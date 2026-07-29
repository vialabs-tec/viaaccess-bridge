#include "ota_update.hpp"

#include <array>
#include <string>
#include <vector>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "viaaccess/ota.hpp"
#include "viaaccess/strings.hpp"
#include "viaaccess/version.hpp"

namespace ota {
namespace {

constexpr const char* kTag = "ota";
constexpr int kHttpTimeoutMs = 5 * 60 * 1000;
constexpr std::size_t kRxChunk = 4096;

std::string HexLower(const uint8_t* data, std::size_t length) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.resize(length * 2);
  for (std::size_t i = 0; i < length; ++i) {
    out[i * 2] = kDigits[(data[i] >> 4) & 0x0f];
    out[i * 2 + 1] = kDigits[data[i] & 0x0f];
  }
  return out;
}

ApplyResult Fail(const std::string& error) {
  ApplyResult result;
  result.ok = false;
  result.error = error;
  return result;
}

}  // namespace

ApplyResult Apply(const std::string& version, const std::string& url,
                  const std::string& sha256_hex) {
  viaaccess::OtaPayload payload;
  payload.version = version;
  payload.url = url;
  payload.sha256 = sha256_hex;
  const viaaccess::OtaPayloadCheck check = viaaccess::ValidateOtaPayload(payload);
  if (!check.ok) {
    return Fail(check.error);
  }

  if (viaaccess::Trim(version) == viaaccess::kFirmwareVersion) {
    ApplyResult result;
    result.ok = true;
    result.already_current = true;
    ESP_LOGI(kTag, "already on version %s, skipping download",
             viaaccess::kFirmwareVersion);
    return result;
  }

  const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
  if (update == nullptr) {
    return Fail("no OTA update partition");
  }

  ESP_LOGI(kTag, "OTA %s -> %s (%u bytes slot at 0x%08lx)",
           viaaccess::kFirmwareVersion, viaaccess::Trim(version).c_str(),
           static_cast<unsigned>(update->size),
           static_cast<unsigned long>(update->address));

  const std::string trimmed_url = viaaccess::Trim(url);
  esp_http_client_config_t http_config = {};
  http_config.url = trimmed_url.c_str();
  http_config.timeout_ms = kHttpTimeoutMs;
  http_config.crt_bundle_attach = esp_crt_bundle_attach;
  http_config.keep_alive_enable = true;
  http_config.buffer_size = static_cast<int>(kRxChunk);
  http_config.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&http_config);
  if (client == nullptr) {
    return Fail("OTA HTTP client init failed");
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return Fail(std::string("OTA open failed: ") + esp_err_to_name(err));
  }

  const int content_length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status < 200 || status >= 300) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return Fail("OTA HTTP " + std::to_string(status));
  }
  if (content_length > static_cast<int>(update->size)) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return Fail("OTA image larger than update slot");
  }

  esp_ota_handle_t ota_handle = 0;
  err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (err != ESP_OK) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return Fail(std::string("esp_ota_begin: ") + esp_err_to_name(err));
  }

  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);

  std::vector<char> buffer(kRxChunk);
  int written = 0;
  bool write_ok = true;
  std::string write_error;

  while (true) {
    const int read = esp_http_client_read(client, buffer.data(),
                                          static_cast<int>(buffer.size()));
    if (read < 0) {
      write_ok = false;
      write_error = "OTA read failed";
      break;
    }
    if (read == 0) {
      if (esp_http_client_is_complete_data_received(client)) {
        break;
      }
      // Yield so the idle task can pet the watchdog during a long download.
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    if (written + read > static_cast<int>(update->size)) {
      write_ok = false;
      write_error = "OTA image larger than update slot";
      break;
    }
    err = esp_ota_write(ota_handle, buffer.data(), static_cast<size_t>(read));
    if (err != ESP_OK) {
      write_ok = false;
      write_error = std::string("esp_ota_write: ") + esp_err_to_name(err);
      break;
    }
    mbedtls_sha256_update(&sha_ctx, reinterpret_cast<const unsigned char*>(buffer.data()),
                          static_cast<size_t>(read));
    written += read;
    if ((written & 0xffff) == 0) {
      ESP_LOGI(kTag, "OTA progress %d bytes", written);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  std::array<uint8_t, 32> digest{};
  mbedtls_sha256_finish(&sha_ctx, digest.data());
  mbedtls_sha256_free(&sha_ctx);
  const std::string got_hash = HexLower(digest.data(), digest.size());

  if (!write_ok) {
    esp_ota_abort(ota_handle);
    return Fail(write_error);
  }
  if (written <= 0) {
    esp_ota_abort(ota_handle);
    return Fail("OTA downloaded empty image");
  }
  if (got_hash != check.sha256_hex) {
    esp_ota_abort(ota_handle);
    ESP_LOGE(kTag, "sha256 mismatch got %s want %s", got_hash.c_str(),
             check.sha256_hex.c_str());
    return Fail("sha256 mismatch");
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    return Fail(std::string("esp_ota_end: ") + esp_err_to_name(err));
  }
  err = esp_ota_set_boot_partition(update);
  if (err != ESP_OK) {
    return Fail(std::string("esp_ota_set_boot_partition: ") + esp_err_to_name(err));
  }

  ESP_LOGI(kTag, "OTA image ready (%d bytes, sha256 ok), reboot required", written);
  ApplyResult result;
  result.ok = true;
  return result;
}

}  // namespace ota
