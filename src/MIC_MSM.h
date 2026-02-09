#pragma once
#include "ESP_I2S.h"
#include "ESP_SR.h"
#include "esp_task_wdt.h"

#include "LVGL_ST77916.h"
#include "SD_Card.h"
#include <SD.h>
#include "FS.h"
#include "AIAssistant.h"

#define I2S_PIN_BCK   15   // Bit clock (input from ESP32 to mic)
#define I2S_PIN_WS    2    // Word select (LRCK)
#define I2S_PIN_DOUT  -1   // Not used (we're not sending)
#define I2S_PIN_DIN   39   // Data line (input from mic to ESP32)

typedef enum {
    MIC_MODE_TO_FILE = 0,        // Save WAV to SD
    MIC_MODE_TO_AI_CLIENT = 1,   // Current AIAssistant_* streaming
    MIC_MODE_TO_WS_SERVER = 2    // New: send to WebSocket clients
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