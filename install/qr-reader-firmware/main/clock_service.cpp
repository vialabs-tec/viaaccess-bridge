#include "clock_service.hpp"

#include <ctime>
#include <mutex>
#include <sys/time.h>

#include "app_state.hpp"
#include "ds3231_driver.hpp"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "viaaccess/time.hpp"

namespace clock_service {
namespace {

constexpr const char* kTag = "clock";

std::mutex g_mutex;
bool g_sntp_started = false;

int64_t NowUnix() { return static_cast<int64_t>(std::time(nullptr)); }

void SetSystemClock(int64_t unix_seconds) {
  timeval now = {};
  now.tv_sec = static_cast<time_t>(unix_seconds);
  settimeofday(&now, nullptr);
}

void PublishRtcStatus(const ds3231::Reading& reading) {
  app::State::Instance().set_rtc_status(ds3231::present(), reading.oscillator_stopped,
                                       reading.temperature_c);
}

// OnSntpSync also pushes the corrected time back into the RTC, which is what
// makes the clock survive the next power cut. Writing on every sync keeps drift
// bounded without a separate schedule.
void OnSntpSync(timeval* /*synced*/) {
  const int64_t now = NowUnix();
  viaaccess::ClockState state;
  state.source = viaaccess::ClockSource::kNetwork;
  state.set_at = now;
  app::State::Instance().set_clock(state);
  ESP_LOGI(kTag, "network time acquired: %s", viaaccess::FormatRfc3339(now).c_str());

  if (!ds3231::present()) {
    return;
  }
  if (ds3231::Write(now) == ESP_OK) {
    const ds3231::Reading reading = ds3231::Read();
    PublishRtcStatus(reading);
    ESP_LOGI(kTag, "battery clock updated from network time");
  }
}

}  // namespace

void Init(const viaaccess::RtcConfig& cfg) {
  const esp_err_t err = ds3231::Init(cfg);
  if (err != ESP_OK) {
    app::State::Instance().set_rtc_status(false, false, 0);
    return;
  }

  const ds3231::Reading reading = ds3231::Read();
  PublishRtcStatus(reading);

  if (reading.oscillator_stopped) {
    ESP_LOGW(kTag, "battery clock lost power, its time is not trustworthy: replace "
                   "the cell and wait for the next network sync");
    return;
  }
  if (!reading.ok || !viaaccess::IsPlausibleUnixTime(reading.unix_seconds)) {
    ESP_LOGW(kTag, "battery clock holds an implausible time, ignoring it");
    return;
  }

  SetSystemClock(reading.unix_seconds);
  viaaccess::ClockState state;
  state.source = viaaccess::ClockSource::kRtc;
  state.set_at = reading.unix_seconds;
  app::State::Instance().set_clock(state);
  ESP_LOGI(kTag, "clock set from battery clock: %s",
           viaaccess::FormatRfc3339(reading.unix_seconds).c_str());
}

void OnNetworkUp() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_sntp_started) {
    return;
  }

  // Without a plausible date every TLS handshake against Identity fails
  // certificate validity checks, so this runs even when the RTC already answered:
  // network time is the authority and the RTC only bridges the outages.
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  config.start = true;
  config.server_from_dhcp = true;
  config.renew_servers_after_new_IP = true;
  config.index_of_first_server = 1;
  config.sync_cb = OnSntpSync;

  const esp_err_t err = esp_netif_sntp_init(&config);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "SNTP init failed: %s", esp_err_to_name(err));
    return;
  }
  g_sntp_started = true;
}

void RefreshRtcTemperature() {
  if (!ds3231::present()) {
    return;
  }
  PublishRtcStatus(ds3231::Read());
}

}  // namespace clock_service
