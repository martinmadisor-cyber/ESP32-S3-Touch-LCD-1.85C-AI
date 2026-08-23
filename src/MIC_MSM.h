#pragma once
#include "ESP_I2S.h"
#include "ESP_SR.h"
#include "esp_task_wdt.h"

#include "LVGL_ST77916.h"
#include "SD_Card.h"
#include <SD.h>
#include "FS.h"
#include "AIAssistant.h"
#include "Chatbot.h"

// The microphone on this board is an ES7210 ADC sharing the I2S bus with the
// ES8311 output codec, not a standalone I2S mic. GPIO15 and GPIO2 belong to
// the amplifier enable and the codec master clock.
#define I2S_PIN_BCK   48   // Bit clock
#define I2S_PIN_WS    38   // Word select (LRCK)
#define I2S_PIN_DOUT  47   // Shared with the output codec
#define I2S_PIN_DIN   39   // Data from the ES7210
#define I2S_PIN_MCK   2    // Master clock, required by the ES7210

typedef enum {
    MIC_MODE_TO_FILE = 0,        // Save WAV to SD
    MIC_MODE_TO_AI_CLIENT = 1,   // Current AIAssistant_* streaming
    MIC_MODE_TO_WS_SERVER = 2,   // Send to WebSocket clients
    MIC_MODE_TO_CHATBOT = 3      // Send to Chatbot WS (OpenAI Realtime)
} mic_mode_t;

typedef struct {
    mic_mode_t mode;
    // Optional: if you ever want per-task filename instead of global
    // const char* filename;
} MicTaskParams;

void MIC_SR_Start();
void MIC_SR_Stop();

void MIC_Init(void);
void MIC_StartRecording(const char* filename, uint32_t rate = 16000, uint8_t ch = 1, uint16_t bits = 16, mic_mode_t stream = MIC_MODE_TO_FILE);
void MIC_StopRecording();