# Transport-free appliance logic shared by the firmware and the host tests.
# Nothing here may include ESP-IDF headers: test/host builds the same sources
# with a plain compiler.
idf_component_register(
  SRCS
    "src/clock.cpp"
    "src/commands.cpp"
    "src/config.cpp"
    "src/contingency.cpp"
    "src/crypto.cpp"
    "src/door_contact.cpp"
    "src/ds3231.cpp"
    "src/exit_button.cpp"
    "src/hostname.cpp"
    "src/line_buffer.cpp"
    "src/mode.cpp"
    "src/ota.cpp"
    "src/outbox.cpp"
    "src/poll.cpp"
    "src/provision.cpp"
    "src/redeem.cpp"
    "src/scan.cpp"
    "src/status_led.cpp"
    "src/strings.cpp"
    "src/time.cpp"
  INCLUDE_DIRS "include"
)
