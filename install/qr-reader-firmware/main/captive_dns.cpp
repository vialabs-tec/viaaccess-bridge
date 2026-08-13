#include "captive_dns.hpp"

#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace captive_dns {
namespace {

constexpr const char* kTag = "captive_dns";
constexpr uint16_t kDnsPort = 53;
constexpr size_t kMaxPacket = 512;
// 192.168.4.1 in network order.
constexpr uint32_t kSoftApIpv4 = 0xC0A80401;

TaskHandle_t g_task = nullptr;
int g_sock = -1;
volatile bool g_stop = false;

void CloseSocket() {
  if (g_sock >= 0) {
    close(g_sock);
    g_sock = -1;
  }
}

// Answer the first question with A 192.168.4.1. Phones send one A query.
int BuildResponse(uint8_t* buf, int len) {
  if (len < 12 || len + 16 > static_cast<int>(kMaxPacket)) {
    return -1;
  }
  const uint16_t questions = (static_cast<uint16_t>(buf[4]) << 8) | buf[5];
  if (questions == 0) {
    return -1;
  }
  buf[2] = 0x84;
  buf[3] = 0x80;
  buf[6] = 0;
  buf[7] = 1;
  buf[8] = 0;
  buf[9] = 0;
  buf[10] = 0;
  buf[11] = 0;

  int pos = len;
  buf[pos++] = 0xC0;
  buf[pos++] = 0x0C;
  buf[pos++] = 0x00;
  buf[pos++] = 0x01;  // A
  buf[pos++] = 0x00;
  buf[pos++] = 0x01;  // IN
  buf[pos++] = 0x00;
  buf[pos++] = 0x00;
  buf[pos++] = 0x00;
  buf[pos++] = 0x3C;  // TTL 60s
  buf[pos++] = 0x00;
  buf[pos++] = 0x04;
  buf[pos++] = static_cast<uint8_t>((kSoftApIpv4 >> 24) & 0xFF);
  buf[pos++] = static_cast<uint8_t>((kSoftApIpv4 >> 16) & 0xFF);
  buf[pos++] = static_cast<uint8_t>((kSoftApIpv4 >> 8) & 0xFF);
  buf[pos++] = static_cast<uint8_t>(kSoftApIpv4 & 0xFF);
  return pos;
}

void DnsTask(void* /*arg*/) {
  uint8_t packet[kMaxPacket];
  while (!g_stop) {
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const int n = recvfrom(g_sock, packet, sizeof(packet), 0,
                           reinterpret_cast<struct sockaddr*>(&from), &from_len);
    if (n <= 0) {
      continue;
    }
    const int out = BuildResponse(packet, n);
    if (out > 0) {
      sendto(g_sock, packet, out, 0, reinterpret_cast<struct sockaddr*>(&from), from_len);
    }
  }
  CloseSocket();
  g_task = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

esp_err_t Start() {
  if (g_task != nullptr && !g_stop) {
    return ESP_OK;
  }
  g_stop = false;
  for (int i = 0; i < 20 && g_task != nullptr; ++i) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (g_task != nullptr) {
    return ESP_OK;
  }
  g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (g_sock < 0) {
    ESP_LOGE(kTag, "socket failed");
    return ESP_FAIL;
  }
  const int yes = 1;
  setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kDnsPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(g_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    ESP_LOGE(kTag, "bind :53 failed");
    CloseSocket();
    return ESP_FAIL;
  }

  struct timeval tv{};
  tv.tv_sec = 1;
  setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (xTaskCreate(DnsTask, "captive_dns", 4096, nullptr, 5, &g_task) != pdPASS) {
    CloseSocket();
    g_task = nullptr;
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "wildcard DNS on :53 (SoftAP captive portal)");
  return ESP_OK;
}

void Stop() {
  g_stop = true;
}

}  // namespace captive_dns
