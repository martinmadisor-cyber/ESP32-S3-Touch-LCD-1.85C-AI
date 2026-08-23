#include <Wire.h>
#include "RTC_PCF85063.h"

// RTC (PCF85063) Helper Functions
uint8_t decToBcd(uint8_t val) {
  return ((val / 10 * 16) + (val % 10));
}

uint8_t bcdToDec(uint8_t val) {
  return ((val / 16 * 10) + (val % 16));
}

void RTC_SetTime(struct tm* t) {
  // Convert to UTC before saving
  time_t local = mktime(t);  // Convert to epoch
  // local -= 3600;             // Subtract your GMT offset (e.g. 3600s for UTC+1)
  struct tm* utc = gmtime(&local);

  constexpr int maxRetries = 3;
  int attempt = 0;

  while (attempt < maxRetries) {
    Wire.beginTransmission(PCF85063_ADDRESS);
    Wire.write(0x04);  // Start at seconds register
    Wire.write(decToBcd(utc->tm_sec));
    Wire.write(decToBcd(utc->tm_min));
    Wire.write(decToBcd(utc->tm_hour));
    Wire.write(decToBcd(utc->tm_mday));
    Wire.write(decToBcd(utc->tm_wday));
    Wire.write(decToBcd(utc->tm_mon + 1));
    Wire.write(decToBcd(utc->tm_year % 100));
    
    if (Wire.endTransmission() == 0) return;
    attempt++;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  Serial.println("[RTC] Failed to set UTC time after retries");
}

// Epoch seconds from a UTC calendar date. timegm() is not available in this
// toolchain and mktime() would apply the local time zone, which is wrong for
// the RTC registers.
static time_t utcToEpoch(const struct tm& t) {
  int y = t.tm_year + 1900;
  unsigned m = t.tm_mon + 1;
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = era * 146097L + (long)doe - 719468L;
  return days * 86400L + t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
}

bool RTC_GetTime(struct tm* t) {
  Wire.beginTransmission(PCF85063_ADDRESS);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0){
     Serial.println("[RTC] Failed to get RTC time, no device");
     return false;
  }

  if (Wire.requestFrom(PCF85063_ADDRESS, 7) != 7){
    Serial.println("[RTC] Failed to get RTC time, wrong data");
    return false;
  }

  t->tm_sec  = bcdToDec(Wire.read() & 0x7F);
  t->tm_min  = bcdToDec(Wire.read() & 0x7F);
  t->tm_hour = bcdToDec(Wire.read() & 0x3F);
  t->tm_mday = bcdToDec(Wire.read() & 0x3F);
  t->tm_wday = bcdToDec(Wire.read() & 0x07);
  t->tm_mon  = bcdToDec(Wire.read() & 0x1F) - 1;
  t->tm_year = bcdToDec(Wire.read()) + 100;

  // The registers hold UTC, so convert them as UTC and let the C library
  // apply the configured time zone. mktime() would read them as local time.
  time_t utc = utcToEpoch(*t);
  struct tm *local = localtime(&utc);
  *t = *local;

  return true;
}
