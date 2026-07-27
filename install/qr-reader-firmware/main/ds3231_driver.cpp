#include "ds3231_driver.hpp"

#include <mutex>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "viaaccess/ds3231.hpp"

namespace ds3231 {
namespace {

constexpr const char* kTag = "ds3231";
constexpr int kBusSpeedHz = 100000;
constexpr int kTimeoutMs = 200;

std::mutex g_mutex;
i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_device = nullptr;
bool g_present = false;

// The i2c_master API takes the timeout in milliseconds, not ticks.
esp_err_t ReadRegisters(uint8_t start, uint8_t* out, size_t length) {
  return i2c_master_transmit_receive(g_device, &start, 1, out, length, kTimeoutMs);
}

esp_err_t WriteRegisters(uint8_t start, const uint8_t* values, size_t length) {
  // One transfer: register address followed by the payload.
  uint8_t payload[1 + viaaccess::kDs3231TimeRegCount];
  if (length + 1 > sizeof(payload)) {
    return ESP_ERR_INVALID_SIZE;
  }
  payload[0] = start;
  for (size_t i = 0; i < length; i++) {
    payload[i + 1] = values[i];
  }
  return i2c_master_transmit(g_device, payload, length + 1, kTimeoutMs);
}

}  // namespace

esp_err_t Init(const viaaccess::RtcConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_present = false;

  if (!cfg.enabled) {
    ESP_LOGI(kTag, "disabled in config, clock will depend on SNTP alone");
    return ESP_ERR_NOT_FOUND;
  }

  if (g_bus == nullptr) {
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = cfg.i2c_port;
    bus.sda_io_num = static_cast<gpio_num_t>(cfg.sda_pin);
    bus.scl_io_num = static_cast<gpio_num_t>(cfg.scl_pin);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    // The ZS-042 module already has 4.7k pull-ups, but the internal ones make a
    // bare chip on a bench harness work without extra parts.
    bus.flags.enable_internal_pullup = true;

    const esp_err_t err = i2c_new_master_bus(&bus, &g_bus);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "i2c bus on sda=%d scl=%d failed: %s", cfg.sda_pin, cfg.scl_pin,
               esp_err_to_name(err));
      g_bus = nullptr;
      return err;
    }
  }

  const esp_err_t probed =
      i2c_master_probe(g_bus, viaaccess::kDs3231Address, kTimeoutMs);
  if (probed != ESP_OK) {
    ESP_LOGW(kTag, "no clock on the bus (sda=%d, scl=%d): offline passage will be "
                   "refused after a power cut",
             cfg.sda_pin, cfg.scl_pin);
    return ESP_ERR_NOT_FOUND;
  }

  if (g_device == nullptr) {
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = viaaccess::kDs3231Address;
    device.scl_speed_hz = kBusSpeedHz;
    const esp_err_t err = i2c_master_bus_add_device(g_bus, &device, &g_device);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "cannot attach device: %s", esp_err_to_name(err));
      g_device = nullptr;
      return err;
    }
  }

  g_present = true;
  ESP_LOGI(kTag, "clock found at 0x%02x (sda=%d, scl=%d)", viaaccess::kDs3231Address,
           cfg.sda_pin, cfg.scl_pin);
  return ESP_OK;
}

bool present() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_present;
}

Reading Read() {
  std::lock_guard<std::mutex> lock(g_mutex);
  Reading reading;
  if (!g_present) {
    return reading;
  }

  uint8_t time_regs[viaaccess::kDs3231TimeRegCount] = {};
  if (ReadRegisters(viaaccess::kDs3231RegTime, time_regs, sizeof(time_regs)) != ESP_OK) {
    ESP_LOGW(kTag, "time registers unreadable");
    return reading;
  }

  uint8_t status = 0;
  if (ReadRegisters(viaaccess::kDs3231RegStatus, &status, 1) != ESP_OK) {
    ESP_LOGW(kTag, "status register unreadable");
    return reading;
  }
  reading.oscillator_stopped = viaaccess::Ds3231OscillatorStopped(status);

  uint8_t temperature[2] = {};
  if (ReadRegisters(viaaccess::kDs3231RegTemperature, temperature,
                    sizeof(temperature)) == ESP_OK) {
    reading.temperature_c =
        viaaccess::DecodeDs3231Temperature(temperature[0], temperature[1]);
  }

  reading.unix_seconds = viaaccess::DecodeDs3231Time(time_regs);
  reading.ok = reading.unix_seconds > 0;
  if (!reading.ok) {
    ESP_LOGW(kTag, "registers do not hold a valid date");
  }
  return reading;
}

esp_err_t Write(int64_t unix_seconds) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_present) {
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t regs[viaaccess::kDs3231TimeRegCount] = {};
  if (!viaaccess::EncodeDs3231Time(unix_seconds, regs)) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = WriteRegisters(viaaccess::kDs3231RegTime, regs, sizeof(regs));
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "cannot write time: %s", esp_err_to_name(err));
    return err;
  }

  // Clearing OSF is what marks the stored time as trustworthy for the next boot;
  // read-modify-write so the alarm flags in the same register survive.
  uint8_t status = 0;
  err = ReadRegisters(viaaccess::kDs3231RegStatus, &status, 1);
  if (err == ESP_OK) {
    const uint8_t cleared = viaaccess::Ds3231ClearOscillatorStopFlag(status);
    if (cleared != status) {
      err = WriteRegisters(viaaccess::kDs3231RegStatus, &cleared, 1);
    }
  }
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "time written but stop flag not cleared: %s", esp_err_to_name(err));
  }
  return ESP_OK;
}

}  // namespace ds3231
