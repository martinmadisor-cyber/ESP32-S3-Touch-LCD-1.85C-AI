// Chatbot.cpp - Real-time voice chatbot via OpenAI Realtime API
// ESP32 ←(binary PCM16)→ Backend ←(JSON+base64)→ OpenAI Realtime API
//
// Audio path:
//   Mic (24kHz PCM16) → WS binary → Backend → OpenAI
//   OpenAI → Backend → WS binary → Ring buffer → I2S TX → PCM5101 DAC → Speaker

#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include "Chatbot.h"
#include "GUI/GUI.h"
#include "PCM5101.h"
#include "MIC_MSM.h"
#include "SD_Card.h"
#include <vector>
#include <utility>
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"

using namespace websockets;

// --- State ---
static WebsocketsClient chatbot_ws;
static volatile bool chatbot_active = false;
static volatile bool chatbot_ready  = false;
static uint32_t lastActivity = 0;
static String lastErrorStr = "";

// --- I2S TX for raw PCM output ---
static i2s_chan_handle_t tx_chan = NULL;

// --- Ring buffer (PSRAM) ---
#define CHATBOT_RINGBUF_SIZE (512 * 1024)
static uint8_t* ringBuf = nullptr;
static volatile size_t rbWrite = 0;
static volatile size_t rbRead  = 0;

// --- Tasks ---
static TaskHandle_t playbackTask = nullptr;
static TaskHandle_t wsPollerTask = nullptr;
static SemaphoreHandle_t wsMutex = NULL;

// ─────────── Helpers ───────────

static const char* chatbotURL = ENV_WEBSOCKET_URL;

static size_t rbAvailable() {
    size_t w = rbWrite, r = rbRead;
    return (w >= r) ? (w - r) : (CHATBOT_RINGBUF_SIZE - r + w);
}

static bool rbPush(const uint8_t* data, size_t len) {
    size_t free = CHATBOT_RINGBUF_SIZE - 1 - rbAvailable();
    if (len > free) return false;
    size_t w = rbWrite;
    for (size_t i = 0; i < len; i++) {
        ringBuf[w] = data[i];
        w = (w + 1) % CHATBOT_RINGBUF_SIZE;
    }
    rbWrite = w;
    return true;
}

static size_t rbPop(uint8_t* dst, size_t maxLen) {
    size_t avail = rbAvailable();
    size_t n = (avail < maxLen) ? avail : maxLen;
    if (n == 0) return 0;
    size_t r = rbRead;
    for (size_t i = 0; i < n; i++) {
        dst[i] = ringBuf[r];
        r = (r + 1) % CHATBOT_RINGBUF_SIZE;
    }
    rbRead = r;
    return n;
}

// ─────────── I2S Output ───────────

static bool initI2S() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Chatbot] i2s_new_channel failed: %s\n", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(24000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK,
            .ws   = (gpio_num_t)I2S_LRC,
            .dout = (gpio_num_t)I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { false, false, false },
        },
    };

    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (err != ESP_OK) {
        Serial.printf("[Chatbot] i2s_channel_init failed: %s\n", esp_err_to_name(err));
        i2s_del_channel(tx_chan); tx_chan = NULL;
        return false;
    }

    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) {
        Serial.printf("[Chatbot] i2s_channel_enable failed: %s\n", esp_err_to_name(err));
        i2s_del_channel(tx_chan); tx_chan = NULL;
        return false;
    }

    Serial.println("[Chatbot] I2S TX ready (24 kHz mono PCM16)");
    return true;
}

static void deinitI2S() {
    if (tx_chan) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
        Serial.println("[Chatbot] I2S TX released");
    }
}

// ─────────── Playback Task ───────────

static void playbackTaskFn(void*) {
    // 10 ms of 24 kHz mono 16-bit = 480 bytes
    uint8_t buf[480];

    while (chatbot_active) {
        size_t avail = rbAvailable();
        if (avail >= sizeof(buf)) {
            size_t n = rbPop(buf, sizeof(buf));
            if (n > 0 && tx_chan) {
                size_t written = 0;
                i2s_channel_write(tx_chan, buf, n, &written, portMAX_DELAY);
            }
        } else {
            // Let the WebSockets task receive more network chunks.
            // The I2S DMA buffer will handle small network jitter smoothly.
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    vTaskDelete(NULL);
}

// ─────────── WebSocket Callbacks ───────────

static void onMsg(WebsocketsMessage msg) {
    if (msg.isBinary()) {
        const uint8_t* d = (const uint8_t*)msg.c_str();
        size_t len = msg.length();
        if (len > 0 && !rbPush(d, len)) {
            Serial.println("[Chatbot] Ring-buffer overflow");
        }
    } else if (msg.isText()) {
        Serial.printf("[Chatbot] TXT: %s\n", msg.data().c_str());
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, msg.data()) == DeserializationError::Ok) {
            const char* t = doc["type"] | "";
            if (strcmp(t, "CHATBOT_READY") == 0)              chatbot_ready = true;
            else if (strcmp(t, "CHATBOT_SPEECH_STARTED") == 0) Serial.println("[Chatbot] VAD: speech start");
            else if (strcmp(t, "CHATBOT_SPEECH_STOPPED") == 0) Serial.println("[Chatbot] VAD: speech stop");
            else if (strcmp(t, "CHATBOT_AUDIO_DONE") == 0)     Serial.println("[Chatbot] response audio done");
            else if (strcmp(t, "CHATBOT_STOP") == 0) {
                Serial.println("[Chatbot] Remote stop command");
                chatbot_active = false; // Trigger task exits
                if (doc.containsKey("reason")) lastErrorStr = String("Stopped: ") + doc["reason"].as<const char*>();
            }
            else if (strcmp(t, "ERROR") == 0) {
                lastErrorStr = doc["message"] | "Remote Error";
                Serial.printf("[Chatbot] Remote ERROR: %s\n", lastErrorStr.c_str());
                Chatbot_Stop();
            }
            else if (strcmp(t, "CHATBOT_ACTION") == 0) {
                const char* action = doc["action"] | "";
                if (strcmp(action, "play_radio") == 0) {
                    const char* station_query = doc["station"] | "";
                    Serial.printf("[Chatbot] Action PLAY RADIO: %s\n", station_query);
                    Chatbot_Stop(); 
                    
                    std::vector<std::pair<String, String>> stations = ReadInternetStations();
                    String matched_url = "";
                    String query_lower = String(station_query);
                    query_lower.toLowerCase();
                    
                    for (const auto& station : stations) {
                        String name_lower = station.first;
                        name_lower.toLowerCase();
                        if (name_lower.indexOf(query_lower) >= 0 || query_lower.indexOf(name_lower) >= 0) {
                            matched_url = station.second;
                            break;
                        }
                    }
                    
                    if (matched_url.length() > 0 && audio_ptr) {
                        Serial.printf("[Chatbot] Tuning to %s\n", matched_url.c_str());
                        audio_ptr->connecttohost(matched_url.c_str());
                        // Trigger GUI update and navigate correctly
                        GUI_SwitchToScreen(GUI_CreateMainScreen, &main_screen);
                    } else {
                        Serial.println("[Chatbot] Radio station not found on SD card");
                    }
                }
            }
        }
        lastActivity = millis();
    }
}

static void onEvt(WebsocketsEvent evt, String) {
    if (evt == WebsocketsEvent::ConnectionOpened) {
        Serial.println("[Chatbot] WS opened");
        lastErrorStr = "";
    } else if (evt == WebsocketsEvent::ConnectionClosed) {
        Serial.println("[Chatbot] WS closed");
        chatbot_ready = false;
        if (chatbot_active) lastErrorStr = "WS Disconnected";
    }
}

static void wsPollerFn(void*) {
    while (chatbot_active) {
        if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            chatbot_ws.poll();
            xSemaphoreGive(wsMutex);
        }
        // 300s timeout check
        if (millis() - lastActivity > 300000) {
            Serial.println("[Chatbot] Idle timeout (>300s)");
            lastErrorStr = "Idle Timeout";
            chatbot_active = false; 
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    // Perform cleanup if we exited the loop internally
    if (chatbot_ws.available() || tx_chan) {
        Chatbot_Stop();
    }
    vTaskDelete(NULL);
}

// ─────────── Public API ───────────

void Chatbot_Init(Audio& audio) {
    if (!wsMutex) wsMutex = xSemaphoreCreateMutex();
    ringBuf = (uint8_t*)heap_caps_malloc(CHATBOT_RINGBUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!ringBuf) ringBuf = (uint8_t*)malloc(CHATBOT_RINGBUF_SIZE);
    Serial.printf("[Chatbot] Init %s\n", ringBuf ? "OK" : "FAIL");
}

void Chatbot_Start() {
    if (chatbot_active) return;
    Chatbot_ClearError();
    Serial.println("[Chatbot] Starting...");

    rbWrite = rbRead = 0;
    chatbot_ready = false;
    chatbot_active = true;
    lastActivity = millis();

    // 1. Release Audio library's I2S
    Audio_Deinit();

    // 2. Claim I2S for raw PCM output
    if (!initI2S()) {
        Serial.println("[Chatbot] I2S init failed");
        Audio_Reinit();
        chatbot_active = false;
        return;
    }

    // 3. Connect WS to same server as AIAssistant (port 8765)
    Serial.printf("[Chatbot] WS → %s\n", chatbotURL);
    chatbot_ws.onMessage(onMsg);
    chatbot_ws.onEvent(onEvt);

    if (!chatbot_ws.connect(chatbotURL)) {
        Serial.println("[Chatbot] WS connect failed");
        lastErrorStr = "Backend Connection Fail";
        deinitI2S();
        Audio_Reinit();
        chatbot_active = false;
        return;
    }

    // Tell the server this connection is a chatbot session
    if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        chatbot_ws.send(R"({"type":"CHATBOT_START"})");
        xSemaphoreGive(wsMutex);
    }

    // 4. Playback task (core 0)
    xTaskCreatePinnedToCore(playbackTaskFn, "CB_Play", 4096, NULL, 3, &playbackTask, 0);

    // 5. WS poller task (core 1)
    xTaskCreatePinnedToCore(wsPollerFn, "CB_WS", 4096, NULL, 3, &wsPollerTask, 1);

    // 6. Start mic at 24 kHz (matches OpenAI pcm16 expected rate)
    MIC_StartRecording("/chatbot.raw", 24000, 1, 16, MIC_MODE_TO_CHATBOT);

    Serial.println("[Chatbot] Running ✓");
}

void Chatbot_Stop() {
    if (!chatbot_active) return;
    Serial.println("[Chatbot] Stopping...");

    MIC_StopRecording();

    chatbot_active = false;
    chatbot_ready  = false;

    if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (chatbot_ws.available()) {
            chatbot_ws.send(R"({"type":"CHATBOT_STOP"})");
            chatbot_ws.close();
        }
        xSemaphoreGive(wsMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    deinitI2S();
    Audio_Reinit();

    Serial.println("[Chatbot] Stopped ✓");
}

bool Chatbot_IsActive() { return chatbot_active; }

static uint8_t txMicBuf[4096];
static size_t txMicLen = 0;

void Chatbot_SendAudioChunk(const void* buf, size_t bytes) {
    if (!chatbot_active || !chatbot_ws.available()) return;

    const uint8_t* ptr = (const uint8_t*)buf;
    while (bytes > 0) {
        size_t space = sizeof(txMicBuf) - txMicLen;
        size_t take = (bytes < space) ? bytes : space;
        memcpy(txMicBuf + txMicLen, ptr, take);
        txMicLen += take;
        ptr += take;
        bytes -= take;

        if (txMicLen >= sizeof(txMicBuf)) {
            if (wsMutex && xSemaphoreTake(wsMutex, portMAX_DELAY) == pdTRUE) {
                if (chatbot_ws.available()) {
                    chatbot_ws.sendBinary(reinterpret_cast<const char*>(txMicBuf), txMicLen);
                }
                xSemaphoreGive(wsMutex);
                txMicLen = 0;
            }
            lastActivity = millis();
        }
    }
}

const char* Chatbot_GetLastError() {
    return lastErrorStr.length() > 0 ? lastErrorStr.c_str() : nullptr;
}

void Chatbot_ClearError() {
    lastErrorStr = "";
}
