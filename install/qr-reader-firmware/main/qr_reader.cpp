#include "qr_reader.hpp"

#include <atomic>
#include <mutex>
#include <string>

#include "app_state.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scan_service.hpp"
#include "viaaccess/line_buffer.hpp"

namespace qr_reader {
namespace {

constexpr const char* kTag = "qr_reader";

// One QR URL fits in a couple hundred bytes; the ring buffer holds several so a
// burst of scans is never lost while the pipeline talks to Identity.
constexpr int kRxBufferBytes = 2048;
constexpr size_t kReadChunkBytes = 256;

std::mutex g_mutex;
viaaccess::QrReaderConfig g_config;
viaaccess::QrReaderConfig g_desired;
bool g_driver_ready = false;
bool g_reconfigure = false;
std::atomic<uint32_t> g_scans{0};
TaskHandle_t g_task = nullptr;

bool SamePort(const viaaccess::QrReaderConfig& a, const viaaccess::QrReaderConfig& b) {
  return a.enabled == b.enabled && a.uart_port == b.uart_port && a.rx_pin == b.rx_pin &&
         a.tx_pin == b.tx_pin && a.baud == b.baud;
}

// Must not run while another task is inside uart_read_bytes: uart_driver_delete
// then trips spinlock_acquire (lock->count == 0) and reboots the chip.
esp_err_t OpenPortLocked(const viaaccess::QrReaderConfig& cfg) {
  if (g_driver_ready) {
    uart_driver_delete(static_cast<uart_port_t>(g_config.uart_port));
    g_driver_ready = false;
  }
  g_config = cfg;
  g_desired = cfg;
  if (!cfg.enabled) {
    ESP_LOGW(kTag, "reader disabled in config, only POST /scan will work");
    return ESP_OK;
  }

  const uart_port_t port = static_cast<uart_port_t>(cfg.uart_port);
  uart_config_t uart = {};
  uart.baud_rate = cfg.baud;
  uart.data_bits = UART_DATA_8_BITS;
  uart.parity = UART_PARITY_DISABLE;
  uart.stop_bits = UART_STOP_BITS_1;
  uart.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_driver_install(port, kRxBufferBytes, 0, 0, nullptr, 0);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "uart_driver_install failed: %s", esp_err_to_name(err));
    return err;
  }
  err = uart_param_config(port, &uart);
  if (err != ESP_OK) {
    uart_driver_delete(port);
    ESP_LOGE(kTag, "uart_param_config failed: %s", esp_err_to_name(err));
    return err;
  }
  // TX is wired even though the module only talks: some EP8280L firmware
  // revisions accept configuration barcodes over the same link.
  err = uart_set_pin(port, cfg.tx_pin, cfg.rx_pin, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    uart_driver_delete(port);
    ESP_LOGE(kTag, "uart_set_pin failed: %s", esp_err_to_name(err));
    return err;
  }

  g_driver_ready = true;
  ESP_LOGI(kTag, "UART%d open at %d baud (rx=%d, tx=%d)", cfg.uart_port, cfg.baud,
           cfg.rx_pin, cfg.tx_pin);
  return ESP_OK;
}

void ReaderLoop(void* /*argument*/) {
  viaaccess::LineBuffer lines;
  char chunk[kReadChunkBytes];

  // Readiness means the port is open, not that something was scanned: an idle
  // door would otherwise report a broken reader for as long as nobody passes.
  // Publishing only on change keeps this off the state mutex every 200 ms.
  bool published = false;
  bool published_ready = false;
  uint32_t published_scans = 0;
  uint32_t published_dropped = 0;
  const auto publish = [&](bool ready) {
    const uint32_t scans = g_scans.load();
    const uint32_t dropped = lines.dropped_lines();
    if (published && ready == published_ready && scans == published_scans &&
        dropped == published_dropped) {
      return;
    }
    app::State::Instance().set_reader_stats(ready, scans, dropped);
    published = true;
    published_ready = ready;
    published_scans = scans;
    published_dropped = dropped;
  };

  for (;;) {
    uart_port_t port = UART_NUM_0;
    bool ready = false;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_reconfigure) {
        OpenPortLocked(g_desired);
        g_reconfigure = false;
        lines.Reset();
      }
      ready = g_driver_ready;
      port = static_cast<uart_port_t>(g_config.uart_port);
    }
    if (!ready) {
      publish(false);
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    publish(true);

    const int read = uart_read_bytes(port, chunk, sizeof(chunk), pdMS_TO_TICKS(200));
    if (read <= 0) {
      continue;
    }

    for (const std::string& line : lines.Feed(chunk, static_cast<size_t>(read))) {
      g_scans.fetch_add(1);
      // The reader task owns the whole passage decision, exactly like the HTTP
      // path: redeem, relay and webhook all happen before the next line is read,
      // which is also what keeps two rapid scans from interleaving.
      scan_service::HandleReaderLine(line);
    }
    publish(true);
  }
}

}  // namespace

esp_err_t Start(const viaaccess::QrReaderConfig& cfg) {
  esp_err_t err = ESP_OK;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_desired = cfg;
    err = OpenPortLocked(cfg);
  }
  if (g_task == nullptr) {
    // The redeem call and its TLS session live on this stack.
    xTaskCreate(ReaderLoop, "va_reader", 8192, nullptr, 5, &g_task);
  }
  return err;
}

esp_err_t ApplyConfig(const viaaccess::QrReaderConfig& cfg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (SamePort(cfg, g_reconfigure ? g_desired : g_config)) {
    return ESP_OK;
  }
  g_desired = cfg;
  if (g_task == nullptr) {
    return OpenPortLocked(cfg);
  }
  // Close/reopen on va_reader after uart_read_bytes returns. Doing it here
  // (httpd task) panics the UART spinlock and drops SoftAP.
  g_reconfigure = true;
  return ESP_OK;
}

}  // namespace qr_reader
