#include "storage.hpp"

#include <cstdio>
#include <vector>

#include "config_json.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace storage {
namespace {

constexpr const char* kTag = "storage";
constexpr const char* kPartitionLabel = "storage";
constexpr const char* kBasePath = "/data";
constexpr const char* kConfigPath = "/data/config.json";
constexpr const char* kConfigTempPath = "/data/config.json.tmp";
constexpr const char* kPolicyPath = "/data/policy-snapshot.json";
constexpr const char* kPolicyTempPath = "/data/policy-snapshot.json.tmp";
constexpr const char* kNoncePath = "/data/consumed-intents.json";
constexpr const char* kNonceTempPath = "/data/consumed-intents.json.tmp";
constexpr const char* kOutboxPath = "/data/outbox.json";
constexpr const char* kOutboxTempPath = "/data/outbox.json.tmp";

constexpr const char* kNvsNamespace = "viaaccess";
constexpr const char* kNvsDeviceKey = "device_key";
constexpr const char* kNvsWifiPassword = "wifi_pw";

std::string ReadFile(const char* path) {
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    return "";
  }
  std::string out;
  char chunk[512];
  std::size_t read = 0;
  while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
    out.append(chunk, read);
  }
  std::fclose(file);
  return out;
}

esp_err_t WriteFileAtomic(const char* path, const char* temp_path, const std::string& body) {
  FILE* file = std::fopen(temp_path, "wb");
  if (file == nullptr) {
    ESP_LOGE(kTag, "cannot open %s for write", temp_path);
    return ESP_FAIL;
  }
  const std::size_t written = std::fwrite(body.data(), 1, body.size(), file);
  const int flushed = std::fflush(file);
  std::fclose(file);
  if (written != body.size() || flushed != 0) {
    std::remove(temp_path);
    ESP_LOGE(kTag, "short write on %s (%u/%u bytes)", temp_path,
             static_cast<unsigned>(written), static_cast<unsigned>(body.size()));
    return ESP_FAIL;
  }
  // LittleFS rename does not replace an existing target, so drop it first. A
  // power cut between the two leaves the temp file, which the next boot
  // ignores; the previous document is only lost if it was already removed.
  std::remove(path);
  if (std::rename(temp_path, path) != 0) {
    ESP_LOGE(kTag, "cannot rename %s to %s", temp_path, path);
    return ESP_FAIL;
  }
  return ESP_OK;
}

std::string ReadNvsString(const char* key) {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return "";
  }
  std::size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err != ESP_OK || length == 0) {
    nvs_close(handle);
    return "";
  }
  std::vector<char> buffer(length);
  err = nvs_get_str(handle, key, buffer.data(), &length);
  nvs_close(handle);
  if (err != ESP_OK) {
    return "";
  }
  return std::string(buffer.data());
}

esp_err_t WriteNvsString(nvs_handle_t handle, const char* key, const std::string& value) {
  if (value.empty()) {
    const esp_err_t err = nvs_erase_key(handle, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
  }
  return nvs_set_str(handle, key, value.c_str());
}

}  // namespace

esp_err_t Mount() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kBasePath;
  conf.partition_label = kPartitionLabel;
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;

  const esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "littlefs mount failed: %s", esp_err_to_name(err));
    return err;
  }

  std::size_t total = 0;
  std::size_t used = 0;
  if (esp_littlefs_info(kPartitionLabel, &total, &used) == ESP_OK) {
    ESP_LOGI(kTag, "littlefs mounted at %s (%u/%u bytes used)", kBasePath,
             static_cast<unsigned>(used), static_cast<unsigned>(total));
  }
  return ESP_OK;
}

viaaccess::RuntimeConfig LoadConfig() {
  const std::string json = ReadFile(kConfigPath);
  viaaccess::RuntimeConfig cfg = config_json::Parse(json);

  const std::string device_key = ReadNvsString(kNvsDeviceKey);
  if (!device_key.empty()) {
    cfg.device_key = device_key;
  }
  const std::string wifi_password = ReadNvsString(kNvsWifiPassword);
  if (!wifi_password.empty()) {
    cfg.wifi.password = wifi_password;
  }

  if (json.empty()) {
    ESP_LOGW(kTag, "no config.json yet, starting from factory defaults");
  }
  return viaaccess::Normalize(std::move(cfg));
}

esp_err_t SaveConfig(const viaaccess::RuntimeConfig& raw) {
  const viaaccess::RuntimeConfig cfg = viaaccess::Normalize(raw);

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs open failed: %s", esp_err_to_name(err));
    return err;
  }
  err = WriteNvsString(handle, kNvsDeviceKey, cfg.device_key);
  if (err == ESP_OK) {
    err = WriteNvsString(handle, kNvsWifiPassword, cfg.wifi.password);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs write failed: %s", esp_err_to_name(err));
    return err;
  }

  return WriteFileAtomic(kConfigPath, kConfigTempPath, config_json::Serialize(cfg));
}

esp_err_t SavePolicySnapshot(const std::string& json) {
  return WriteFileAtomic(kPolicyPath, kPolicyTempPath, json);
}

std::string LoadPolicySnapshot() { return ReadFile(kPolicyPath); }

esp_err_t SaveNonceStore(const std::string& json) {
  return WriteFileAtomic(kNoncePath, kNonceTempPath, json);
}

std::string LoadNonceStore() { return ReadFile(kNoncePath); }

esp_err_t SaveOutbox(const std::string& json) {
  return WriteFileAtomic(kOutboxPath, kOutboxTempPath, json);
}

std::string LoadOutbox() { return ReadFile(kOutboxPath); }

}  // namespace storage
