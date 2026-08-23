#include "MIC_MSM.h"
#include "es7210.h"
#include "PCM5101.h"
#include "HttpServer.h" 
// English wakeword : Hi ESP！！！！

#include "esp_dsp.h"
#include <math.h>

// ICS-43434
// https://invensense.tdk.com/wp-content/uploads/2016/02/DS-000069-ICS-43434-v1.2.pdf
// Digital I²S interface with high precision 24-bit data
// Wide frequency response from 60 Hz to 20 kHz
// High power supply rejection: −100 dB FS

I2SClass i2s;

static TaskHandle_t micTaskHandle = nullptr;
static File wavFile;
static volatile bool isRecording = false;
//static bool streamToServer = false;  // Stream to server via WebSocket or save to SD card file

static uint32_t sampleRate;
static uint8_t channels;
static uint16_t bitsPerSample;

// Generated using the following command:
// python3 tools/gen_sr_commands.py "Turn on the light,Switch on the light;Turn off the light,Switch off the light,Go dark;Start fan;Stop fan"

enum {
  SR_CMD_TURN_ON_THE_BACKLIGHT,
  SR_CMD_TURN_OFF_THE_BACKLIGHT,
  SR_CMD_BACKLIGHT_IS_BRIGHTEST,
  SR_CMD_BACKLIGHT_IS_DARKEST,
  SR_CMD_PLAY_MUSIC,
};

static const sr_cmd_t sr_commands[] = {
  {0, "Turn on the backlight"},                 // English
  {1, "Turn off the backlight"},                // English
  {2, "backlight is brightest"},                // English
  {3, "backlight is darkest"},                  // English
  {4, "play music"},                             // English
};

bool play_Music_Flag = 0;
uint8_t LCD_Backlight_original = 0;


void Awaken_Event(sr_event_t event, int command_id, int phrase_id) {
  switch (event) {
    case SR_EVENT_WAKEWORD: 
      // if(ACTIVE_TRACK_CNT)
      //   _lv_demo_music_pause();
      printf("WakeWord Detected!\r\n"); 
      //LCD_Backlight_original = LCD_Backlight;
      break;
    case SR_EVENT_WAKEWORD_CHANNEL:
      printf("WakeWord Channel %d Verified!\r\n", command_id);
      // Turn on backlight
      LCD_SetBacklight(true);
      // ESP_SR.setMode(SR_MODE_COMMAND);  // Switch to Command detection
      // LCD_Backlight = 35;
      ESP_SR.setMode(SR_MODE_WAKEWORD); // Switch back to WakeWord detection
      break;
    case SR_EVENT_TIMEOUT:
      printf("Timeout Detected!\r\n");
      ESP_SR.setMode(SR_MODE_WAKEWORD);  // Switch back to WakeWord detection
      //LCD_Backlight = LCD_Backlight_original;
      if(play_Music_Flag){
        play_Music_Flag = 0;
        printf("SR_EVENT_TIMEOUT");
        // if(ACTIVE_TRACK_CNT)
        //   _lv_demo_music_resume();   
        // else
        //   printf("No MP3 file found in SD card!\r\n");    
      }
      break;
    case SR_EVENT_COMMAND:
      printf("Command %d Detected! %s\r\n", command_id, sr_commands[phrase_id].str);
      switch (command_id) {
        // case SR_CMD_HELLO:
        //   Serial.println("Hello, world!");
        //   break;
        case SR_CMD_TURN_ON_THE_BACKLIGHT:      
          // LCD_Backlight = 100;  
          break;
        case SR_CMD_TURN_OFF_THE_BACKLIGHT:     
          //LCD_Backlight = 0;    
          break;
        case SR_CMD_BACKLIGHT_IS_BRIGHTEST:     
          //LCD_Backlight = 100;  
          break;
        case SR_CMD_BACKLIGHT_IS_DARKEST:       
          //LCD_Backlight = 30;   
          break;
        case SR_CMD_PLAY_MUSIC:                 
          play_Music_Flag = 1;              
          break;
        default:                        printf("Unknown Command!\r\n"); break;
      }
      ESP_SR.setMode(SR_MODE_COMMAND);  // Allow for more commands to be given, before timeout
      // ESP_SR.setMode(SR_MODE_WAKEWORD); // Switch back to WakeWord detection
      break;
    default: printf("Unknown Event!\r\n"); break;
  }
}


// The ES7210 is deaf until configured over I2C, the same way the ES8311 is
// mute until configured. Values taken from Waveshare's 08_esp_sr example.
static es7210_dev_handle_t es7210_handle = NULL;

static bool Mic_CodecInit() {
  if (es7210_handle) return true;

  es7210_i2c_config_t i2c_conf = { .i2c_addr = 0x40 };
  if (es7210_new_codec(&i2c_conf, &es7210_handle) != ESP_OK) {
    Serial.println("[MIC] es7210_new_codec failed");
    return false;
  }

  es7210_codec_config_t conf = {};
  conf.i2s_format = ES7210_I2S_FMT_I2S;
  conf.mclk_ratio = 256;
  conf.sample_rate_hz = 16000;
  conf.bit_width = ES7210_I2S_BITS_16B;
  conf.mic_bias = ES7210_MIC_BIAS_2V87;
  conf.mic_gain = ES7210_MIC_GAIN_33DB;
  conf.flags.tdm_enable = false;
  if (es7210_config_codec(es7210_handle, &conf) != ESP_OK) {
    Serial.println("[MIC] es7210_config_codec failed");
    return false;
  }
  es7210_config_volume(es7210_handle, 18);
  Serial.println("[MIC] ES7210 ready");
  return true;
}

void _MIC_Init() {
  Serial.printf("MIC Init\n");

  if (!Mic_CodecInit()) return;

  // Input and output share one I2S bus at different rates, so playback has to
  // let go of it before recording can start.
  Audio_Deinit();

  i2s.setPins(I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DOUT, I2S_PIN_DIN, I2S_PIN_MCK);
  i2s.setTimeout(1000);

  if (!i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.printf("[MIC_Init] I2S begin failed\n");
    Audio_Reinit();
    return;
  }
}

void MIC_SR_Start() {
  Serial.printf("Skip starting MIC SR\n");
  return;


  // This is not fully working yet, so skip it for now
  Serial.printf("MIC SR Start\n");
  ESP_SR.onEvent(Awaken_Event);
  ESP_SR.begin(i2s, sr_commands, sizeof(sr_commands) / sizeof(sr_cmd_t), SR_CHANNELS_MONO, SR_MODE_WAKEWORD);
  ESP_SR.setMode(SR_MODE_WAKEWORD);
}

void MIC_SR_Stop() {
  Serial.printf("Skip stopping MIC SR\n");
  return;

  // This is not fully working yet, so skip it for now
  Serial.printf("MIC SR Stop\n");
  ESP_SR.end();
}


void MICTask(void *parameter) {
  // Only bring the codec up over I2C here. Taking the I2S bus at boot would
  // steal it from playback, which shares it.
  Mic_CodecInit();
  esp_task_wdt_add(NULL);
  while(1){
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelete(NULL);
  
}
void MIC_Init() {
  Serial.printf("Starting MIC Task\n");
  
  xTaskCreatePinnedToCore(
    MICTask,     
    "MICTask",  
    4096,                
    NULL,                 
    5,                   
    NULL,                 
    0                     
  );
}

static void writeLE16(File &file, uint16_t value) {
    uint8_t bytes[2] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };
    file.write(bytes, 2);
}

static void writeLE32(File &file, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF)
    };
    file.write(bytes, 4);
}

static void writeWavHeader(File &file) {
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;

    const char riff[] = "RIFF";
    const char wave[] = "WAVE";
    const char fmt[]  = "fmt ";
    const char data[] = "data";

    file.write((const uint8_t *)riff, 4);       // ChunkID: "RIFF"
    writeLE32(file, 0);                         // ChunkSize: placeholder
    file.write((const uint8_t *)wave, 4);       // Format: "WAVE"

    file.write((const uint8_t *)fmt, 4);        // Subchunk1ID: "fmt "
    writeLE32(file, 16);                        // Subchunk1Size: 16 for PCM
    writeLE16(file, 1);                         // AudioFormat: 1 = PCM
    writeLE16(file, channels);                  // NumChannels
    writeLE32(file, sampleRate);                // SampleRate
    writeLE32(file, byteRate);                  // ByteRate
    writeLE16(file, blockAlign);                // BlockAlign
    writeLE16(file, bitsPerSample);             // BitsPerSample

    file.write((const uint8_t *)data, 4);       // Subchunk2ID: "data"
    writeLE32(file, 0);                         // Subchunk2Size: placeholder
}

static void finalizeWavFile(File &file) {
    uint32_t fileSize = file.size();
    if (fileSize < 44) return;  // File too short to be valid

    String path = file.path();
    file.flush();
    file.close();

    // Reopen for update. Seeking on the append-mode handle appended instead of
    // rewriting, so both size fields stayed at their zero placeholders and the
    // file was not a valid WAV.
    File f = SD_MMC.open(path.c_str(), "r+");
    if (!f) {
        Serial.printf("[MIC] cannot reopen %s to finalize header\n", path.c_str());
        return;
    }
    f.seek(4);
    writeLE32(f, fileSize - 8);
    f.seek(40);
    writeLE32(f, fileSize - 44);
    f.flush();
    f.close();
}

// This works and it is really good 
// Band-pass filtering (ESP-DSP)
// Automatic Gain Control (AGC)
// Send stream to Websocket or save to WAV file
// Clean I2S shutdown

static void MIC_RecordTask(void *parameter) {
    MicTaskParams* cfg = (MicTaskParams*)parameter;
    mic_mode_t mode = cfg->mode;
    vPortFree(cfg);

    int32_t rawBuffer[256];           // 32-bit input from ICS-43434
    float floatSamples[256];          // Float samples for DSP
    float filtered[256];              // Filtered output
    int16_t finalSamples[256];        // Final 16-bit output for WAV

    Serial.println("[MIC] Recording task with bandpass + AGC started");

    // Band-pass filter setup (ESP-DSP biquad IIR)
    // float coeffs[5];
    // float w[2] = {0};
    // const float fs = (float)sampleRate;
    // const float f0 = 1000.0f;  // Center frequency
    // const float Q = 0.707f;    // Quality factor

    // dsps_biquad_gen_bpf_f32(coeffs, f0 / fs, Q);

    // AGC parameters
    float targetLevel = 8000.0f;
    float agcGain = 1.0f;
    float agcAttack = 0.01f;
    float agcRelease = 0.001f;
    uint32_t totalSize = 0;
    uint8_t decimState = 0; // Tracks the 3:2 decimation ratio

    // Streaming
    // Add this near top of function:
    // Optional streaming buffer
    // int16_t* streamBuffer = (int16_t*)heap_caps_malloc(2048 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    // int16_t streamBuffer[2048];   // 2048 * 2B (int16_t) = 4096 Bytes
    // size_t streamOffset = 0;

    while (isRecording) {
        size_t bytesRead = i2s.readBytes((char *)rawBuffer, sizeof(rawBuffer));
        // The ES7210 delivers 16-bit stereo frames, so every 4 bytes carry two
        // samples, not one 32-bit sample. Reading them as 32-bit is what turned
        // recordings into a screech. Keep the left channel.
        const int16_t* stereo = (const int16_t*)rawBuffer;
        size_t sampleCount = bytesRead / (2 * sizeof(int16_t));
        if (sampleCount == 0) continue;

        for (size_t i = 0; i < sampleCount; ++i) {
            floatSamples[i] = (float)stereo[i * 2];
        }
       
        //copy floatSamples to filtered as initial
        memcpy(filtered, floatSamples, sampleCount * sizeof(float));

        // Apply band-pass filter
        // dsps_biquad_f32(floatSamples, filtered, sampleCount, coeffs, w);

        // Estimate RMS for AGC
        float sumSquares = 0.0f;
        for (size_t i = 0; i < sampleCount; ++i) {
            sumSquares += filtered[i] * filtered[i];
        }
        float rms = sqrtf(sumSquares / sampleCount);

        // AGC gain update
        if (rms > 1500.0f) { // Noise floor threshold (calibrated for the ES7210)
            float desiredGain = targetLevel / rms;
            // Cap maximum gain to prevent amplifying pure static into deafening noise
            // Hardware gain already brings voice near half scale; amplifying
            // again clipped every sample to 32748. Only allow attenuation.
            if (desiredGain > 1.0f) desiredGain = 1.0f;
            
            if (desiredGain > agcGain)
                agcGain += agcAttack * (desiredGain - agcGain);
            else
                agcGain += agcRelease * (desiredGain - agcGain);
        } else {
            // In absolute silence, slowly return gain to 1.0
            agcGain += agcRelease * (1.0f - agcGain);
        }

        // Apply AGC gain
        dsps_mulc_f32_ae32(filtered, filtered, sampleCount, agcGain, 1, 1);

        // Convert to int16 safely
        for (size_t i = 0; i < sampleCount; ++i) {
            float s = filtered[i];
            if (s > 32767.0f) s = 32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            finalSamples[i] = (int16_t)s;
        }

        // 24kHz -> 16kHz Decimation (Drop 1 out of every 3 samples)
        int16_t samples16k[256];
        size_t count16k = 0;
        
        if (sampleRate == 24000) {
            for (size_t i = 0; i < sampleCount; ++i) {
                if (decimState != 2) {
                    samples16k[count16k++] = finalSamples[i];
                }
                decimState = (decimState + 1) % 3;
            }
        } else {
            // Passthrough if already at a non-24k rate
            for (size_t i = 0; i < sampleCount; ++i) {
                samples16k[count16k++] = finalSamples[i];
            }
        }

        // MODE-SPECIFIC HANDLING
        switch (mode) {
          case MIC_MODE_TO_AI_CLIENT:
            AIAssistant_SendAudioChunk(samples16k,
                                       count16k * sizeof(int16_t));
            break;

          case MIC_MODE_TO_FILE:
            wavFile.write((uint8_t *)samples16k,
                          count16k * sizeof(int16_t));
            break;

          case MIC_MODE_TO_WS_SERVER:
            // New "server" mode: send to walkie-talkie WebSocket
            wsSendAudioChunk(samples16k,
                                        count16k * sizeof(int16_t));
            break;

          case MIC_MODE_TO_CHATBOT:
            Chatbot_SendAudioChunk(samples16k,
                                   count16k * sizeof(int16_t));
            break;
        }

        // if (streamToServer) {
        //     // Send to WebSocket queue or buffer here
        //     AIAssistant_SendAudioChunk(finalSamples, sampleCount * sizeof(int16_t));

        //     /*
        //     // Check if adding the new samples would overflow the buffer
        //     if (streamOffset + sampleCount >= 1024) {
        //         // Fill up remaining space
        //         size_t spaceLeft = 1024 - streamOffset;
        //         memcpy(&streamBuffer[streamOffset], finalSamples, spaceLeft * sizeof(int16_t));

        //         // Send full buffer
        //         AIAssistant_SendAudioChunk(streamBuffer, 1024 * sizeof(int16_t));
        //         totalSize += 1024 * sizeof(int16_t);
        //         streamOffset = 0;

        //         // Copy remaining samples to start of buffer
        //         size_t remaining = sampleCount - spaceLeft;
        //         if (remaining > 0) {
        //             memcpy(&streamBuffer[streamOffset], &finalSamples[spaceLeft], remaining * sizeof(int16_t));
        //             streamOffset += remaining;
        //         }
        //     } else {
        //         // Enough space, just copy in
        //         memcpy(&streamBuffer[streamOffset], finalSamples, sampleCount * sizeof(int16_t));
        //         streamOffset += sampleCount;
        //     }
        //     */

        // } else {
        //     // Write to WAV file on SD card
        //     wavFile.write((uint8_t *)finalSamples, sampleCount * sizeof(int16_t));
        // }

        totalSize += count16k * sizeof(int16_t);
        // Removed vTaskDelay because i2s.readBytes is already blocking and yielding. 
        // Delaying here drops 50% of the audio frames!
    }
    // Cleanup
    // heap_caps_free(streamBuffer);

    i2s.end();
    Audio_Reinit();
    // delay(50);
    // i2s.~I2SClass();
    // new (&i2s) I2SClass();

    Serial.printf("[MIC] Recording task ended, %d bytes\n", totalSize);

    // Mode-specific finalization
    switch (mode) {
      case MIC_MODE_TO_AI_CLIENT:
        AIAssistant_StopStream();
        break;

      case MIC_MODE_TO_FILE:
        finalizeWavFile(wavFile);
        wavFile.flush();
        wavFile.close();
        break;

      case MIC_MODE_TO_WS_SERVER:
        // Optionally: notify WebSocket clients that mic stopped
        // e.g. WalkieTalkie_StopStream();
        break;

      case MIC_MODE_TO_CHATBOT:
        // Chatbot handles its own cleanup
        break;
    }

    /*
    if (streamToServer) {
        // Flush buffer
        
        //if (streamOffset > 0) {
        //    AIAssistant_SendAudioChunk(streamBuffer, streamOffset * sizeof(int16_t));
         //   totalSize += streamOffset * sizeof(int16_t);
        //    streamOffset = 0;
        //}
        AIAssistant_StopStream();
    } else {
      finalizeWavFile(wavFile);
      wavFile.flush();
      wavFile.close();
    }
    */
    delay(200);
    vTaskDelete(nullptr);
}

// Start recording audio from the microphone
/*
void MIC_StartRecording(const char* filename, uint32_t rate, uint8_t ch, uint16_t bits, bool stream) {
  //  filename.c_str(), 16000 /bitRate/, 1 /chanels/, 16 /bits/);
  if (isRecording) return;

  streamToServer = stream;

  Serial.printf("[MIC] Starting recording: %s at %luHz, %dch, %dbit, stream:%d \n", filename, rate, ch, bits, stream);

  // Configure and start ESP_I2S
  i2s.setPins(I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DOUT, I2S_PIN_DIN);  // Only DOUT or DIN needed
  i2s.setTimeout(1000);  // Optional, useful for .readBytes()

  // Mic outputs 32-bit data format, Mono, Right Channel only
  if (!i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_RIGHT)) {
    Serial.println("[ERR] I2S begin() failed");
    return;
  }

  // Optionally: configure RX data transform if 32-bit mic output
  // if (!i2s.configureRX(rate, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_RX_TRANSFORM_32_TO_16)) {
  //   Serial.println("[ERR] I2S configureRX failed");
  //   return;
  // }

  sampleRate = rate;
  channels = ch;
  bitsPerSample = bits;

  if (stream) {
    // WebSocket stream
    AIAssistant_StartStream();
    Serial.println("[MIC] Start streaming via websocket");
  }else{
    // WAV recording
    if (!SD_MMC.begin()) {
      Serial.println("[ERR] SD_MMC mount failed");
      return;
    }

    // Remove old file if it exists
    SD_MMC.remove(filename);
    wavFile = SD_MMC.open(filename, "w+");
    if (!wavFile) {
      Serial.println("[ERR] Failed to open file for writing");
      return;
    }

    Serial.printf("[MIC] Start Recording %s\n", filename);

    // Write WAV header placeholder
    writeWavHeader(wavFile);
  }

  // Start recording task
  isRecording = true;
  BaseType_t result = xTaskCreatePinnedToCore(
    MIC_RecordTask,       // Task function
    "MIC_RecordTask",     // Name
    8192,                 // Stack size
    NULL,                 // Parameters
    2,                    // Priority
    &micTaskHandle,       // Out handle
    1                     // Core (use 1 for mic tasks)
  );

  if (result != pdPASS) {
    Serial.println("[ERR] Failed to start MIC_RecordTask");
    wavFile.close();
    isRecording = false;
  }
}
*/
void MIC_StartRecording(const char* filename,
                        uint32_t rate,
                        uint8_t ch,
                        uint16_t bits,
                        mic_mode_t mode) {
  if (isRecording) return;

  sampleRate   = rate;
  channels     = ch;
  bitsPerSample = bits;

  Serial.printf("[MIC] Starting recording: %s at %luHz, %dch, %dbit, mode:%d\n",
                filename, rate, ch, bits, (int)mode);

  // Playback owns the shared I2S bus, so it has to let go first.
  if (!Mic_CodecInit()) return;
  Audio_Deinit();

  // Configure and start ESP_I2S. The ES7210 needs MCLK and delivers 16-bit
  // stereo, unlike the standalone mic this code was written for.
  i2s.setPins(I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DOUT, I2S_PIN_DIN, I2S_PIN_MCK);
  i2s.setTimeout(1000);

  if (!i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO)) {
    Serial.println("[ERR] I2S begin() failed");
    Audio_Reinit();
    return;
  }

  // Mode-specific setup
  switch (mode) {
    case MIC_MODE_TO_AI_CLIENT:
      // Existing behaviour: stream to AI server
      AIAssistant_StartStream();
      Serial.println("[MIC] AI client streaming via websocket");
      break;

    case MIC_MODE_TO_FILE: {
      if (!SD_MMC.begin()) {
        Serial.println("[ERR] SD_MMC mount failed");
        i2s.end();
        return;
      }

      SD_MMC.remove(filename);
      wavFile = SD_MMC.open(filename, "w+");
      if (!wavFile) {
        Serial.println("[ERR] Failed to open file for writing");
        i2s.end();
        return;
      }

      Serial.printf("[MIC] Start Recording to file %s\n", filename);
      writeWavHeader(wavFile);
      break;
    }

    case MIC_MODE_TO_WS_SERVER:
      // New walkie-talkie/server mode:
      Serial.println("[MIC] Start streaming to WebSocket clients (server mode)");
      break;

    case MIC_MODE_TO_CHATBOT:
      Serial.println("[MIC] Start streaming to Chatbot (OpenAI Realtime)");
      break;
  }

  // Prepare task params
  MicTaskParams* params = (MicTaskParams*)pvPortMalloc(sizeof(MicTaskParams));
  if (!params) {
    Serial.println("[ERR] Failed to alloc MicTaskParams");
    if (mode == MIC_MODE_TO_FILE && wavFile) wavFile.close();
    if (mode == MIC_MODE_TO_AI_CLIENT)      AIAssistant_StopStream();
    i2s.end();
    return;
  }
  params->mode = mode;

  isRecording = true;
  BaseType_t result = xTaskCreatePinnedToCore(
    MIC_RecordTask,
    "MIC_RecordTask",
    8192,
    params,
    2,
    &micTaskHandle,
    1
  );

  if (result != pdPASS) {
    Serial.println("[ERR] Failed to start MIC_RecordTask");
    vPortFree(params);
    if (mode == MIC_MODE_TO_FILE && wavFile) wavFile.close();
    if (mode == MIC_MODE_TO_AI_CLIENT)      AIAssistant_StopStream();
    i2s.end();
    isRecording = false;
  }
}


void MIC_StopRecording() {
  if (!isRecording) return;
  isRecording = false;

  if (micTaskHandle) {
    while (eTaskGetState(micTaskHandle) != eDeleted) {
      delay(10);
    }
    micTaskHandle = nullptr;
  }

  Serial.println("[MIC] Stopped recording");
}


