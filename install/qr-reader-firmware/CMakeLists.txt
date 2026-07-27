cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# VIAACCESS_FIRMWARE_VERSION is stamped into the binary and reported to Identity
# in X-ViaAccess-Agent-Version. Release builds pass -DVIAACCESS_FIRMWARE_VERSION.
if(NOT DEFINED VIAACCESS_FIRMWARE_VERSION)
  set(VIAACCESS_FIRMWARE_VERSION "dev")
endif()
add_compile_definitions(VIAACCESS_FIRMWARE_VERSION="${VIAACCESS_FIRMWARE_VERSION}")

# Same string in the app descriptor, which is what the OTA tooling and
# esp_ota_get_app_description report. Without this the descriptor stays at the
# IDF default of "1" while /health reports the real version.
set(PROJECT_VER "${VIAACCESS_FIRMWARE_VERSION}")

project(viaaccess-qr-firmware)
