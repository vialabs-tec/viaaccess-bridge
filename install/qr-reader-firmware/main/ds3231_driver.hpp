// I2C transport for the DS3231. All register semantics live in
// viaaccess/ds3231.hpp so they stay unit tested; this file only moves bytes.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "viaaccess/config.hpp"

namespace ds3231 {

struct Reading {
  bool ok = false;
  // Unix seconds, only meaningful when ok is true.
  int64_t unix_seconds = 0;
  // oscillator_stopped means the chip lost power to its cell at some point, so
  // the registers may look sane while holding a time from before the outage.
  bool oscillator_stopped = false;
  double temperature_c = 0;
};

// Init probes the bus and returns ESP_ERR_NOT_FOUND when no chip answers, which
// is a supported configuration: the appliance then relies on SNTP alone.
esp_err_t Init(const viaaccess::RtcConfig& cfg);

bool present();

Reading Read();

// Write pushes the current time into the chip and clears the oscillator stop
// flag, which is what makes the next boot trust the reading.
esp_err_t Write(int64_t unix_seconds);

}  // namespace ds3231
