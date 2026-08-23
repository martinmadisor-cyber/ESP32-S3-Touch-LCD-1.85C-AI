#include "Audio.h"
#include "PCM5101.h"
#include <EEPROM.h>
#include "config.h"
#include "es8311.h"

#define DEFAULT_VOLUME     10      // fallback if uninitialized

static uint8_t currentVolume = DEFAULT_VOLUME;

Audio audio;

// Guard flag: prevents audio.loop() from running on a destroyed object
static volatile bool audio_tick_enabled = true;

void IRAM_ATTR increase_audio_tick(void *arg)
{
  if (audio_tick_enabled) audio.loop();
}

uint8_t LoadVolumeFromEEPROM() {
  uint8_t vol = EEPROM.read(EEPROM_VOLUME_ADDR);
  Serial.printf("Load volume from EEPROM %d\n", vol);
  if (vol > Volume_MAX) vol = DEFAULT_VOLUME;  // Sanity check
  return vol;
}

// The ES8311 boots muted and deaf: without this I2C setup it ignores the I2S
// stream entirely, which is why the speaker stayed silent with a perfectly
// decoded stream. Taken from Waveshare's own 03_audio_out_no_tf example.
bool Codec_Init(uint32_t sampleRate) {
  es8311_handle_t es = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (!es) {
    Serial.println("[ES8311] create failed");
    return false;
  }

  const es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = sampleRate * 256,   // the I2S driver emits MCLK at 256x
    .sample_frequency = sampleRate
  };

  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("[ES8311] init failed");
    return false;
  }
  es8311_voice_volume_set(es, 80, NULL);
  es8311_microphone_config(es, false);

  // Enable the speaker power amplifier.
  pinMode(PA_ENABLE_PIN, OUTPUT);
  digitalWrite(PA_ENABLE_PIN, HIGH);

  Serial.printf("[ES8311] codec ready at %u Hz\n", sampleRate);
  return true;
}

void Audio_Init() {
  EEPROM.begin(EEPROM_SIZE);
  currentVolume = LoadVolumeFromEEPROM();

  // The audio library resamples everything to 48 kHz stereo and pins the I2S
  // clock there, so the codec must be configured for 48 kHz too or the master
  // clock ratio is wrong and nothing comes out.
  Codec_Init(48000);

  // Audio
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
  audio.setVolume(currentVolume); // 0...21  

  // Set up a hardware timer using ESP-IDF's esp_timer to periodically call the audio.loop() function,
  // which is critical for continuous audio playback using the ESP32-audioI2S library.
  // Using a timer for this is more efficient than doing it in loop() or polling with millis().
  esp_timer_handle_t audio_tick_timer = NULL;
  const esp_timer_create_args_t audio_tick_timer_args = {
    .callback = &increase_audio_tick,        // Function to call
    .dispatch_method = ESP_TIMER_TASK,       // Run in ESP-IDF timer task (not ISR)
    .name = "audio_tick",                    // Debug-friendly name
    .skip_unhandled_events = true            // Skip if missed (prevents backlog)
  };
  esp_timer_create(&audio_tick_timer_args, &audio_tick_timer);
  esp_timer_start_periodic(audio_tick_timer, Audio_TICK_PERIOD_MS * 1000);
}

void Audio_Deinit() {
  // Prevent timer callback from calling audio.loop() on destroyed object
  audio_tick_enabled = false;
  vTaskDelay(pdMS_TO_TICKS(50));  // Wait for any in-flight callback

  audio.stopSong();
  vTaskDelay(pdMS_TO_TICKS(20));

  // Explicitly destroy the Audio object (releases its internal I2S channel)
  audio.~Audio();
  Serial.println("[Audio] Deinitialized - I2S released");
}

void Audio_Reinit() {
  // Reconstruct Audio object in-place (same memory as the global `audio`)
  new (&audio) Audio();
  Codec_Init(48000);
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
  audio.setVolume(currentVolume);

  // Re-enable the timer callback
  audio_tick_enabled = true;
  Serial.println("[Audio] Reinitialized");
}

void SetVolume(uint8_t vol) {
  if (vol > Volume_MAX) {
    printf("Audio : The volume value is incorrect. Please enter 0 to 21\r\n");
    return;
  }

  currentVolume = vol;
  audio.setVolume(currentVolume); // 0...21    
  EEPROM.write(EEPROM_VOLUME_ADDR, currentVolume);
  EEPROM.commit();
}

uint8_t GetVolume() {
  return currentVolume;
}