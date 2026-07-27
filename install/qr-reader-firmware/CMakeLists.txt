cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# VIAACCESS_FIRMWARE_VERSION is stamped into the binary and reported to Identity
# in X-ViaAccess-Agent-Version. Release builds pass -DVIAACCESS_FIRMWARE_VERSION.
if(NOT DEFINED VIAACCESS_FIRMWARE_VERSION)
  set(VIAACCESS_FIRMWARE_VERSION "dev")
endif()
add_compile_definitions(VIAACCESS_FIRMWARE_VERSION="${VIAACCESS_FIRMWARE_VERSION}")

project(viaaccess-qr-firmware)
