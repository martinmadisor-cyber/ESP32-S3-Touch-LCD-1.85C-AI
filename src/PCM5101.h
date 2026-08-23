#pragma once

#define I2S_DOUT      47
#define I2S_BCLK      48
#define I2S_LRC       38
// This board carries an ES8311 codec, not a bare PCM5101 DAC. The codec needs
// a master clock and its power amplifier has an enable pin.
#define I2S_MCLK      2
#define PA_ENABLE_PIN 15

#define Audio_TICK_PERIOD_MS  20 // 20ms, 50 times per second
#define Volume_MAX  21

extern Audio audio;

void Audio_Init();
void Audio_Deinit();   // Destroy Audio object, release I2S (for chatbot takeover)
void Audio_Reinit();   // Reconstruct Audio object, reclaim I2S
void SetVolume(uint8_t vol);
uint8_t GetVolume();

