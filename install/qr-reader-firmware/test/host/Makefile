# Builds the transport-free appliance logic with the host compiler so the port
# can be verified without an ESP-IDF install or a board attached.
#
#   make        # build and run
#   make clean

CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1 -g
CORE_DIR := ../../components/viaaccess_core
BUILD_DIR := build
BINARY := $(BUILD_DIR)/viaaccess-core-tests

SOURCES := \
	main.cpp \
	test_after_hours.cpp \
	test_buzzer.cpp \
	test_clock.cpp \
	test_commands.cpp \
	test_config.cpp \
	test_contingency.cpp \
	test_door_contact.cpp \
	test_ds3231.cpp \
	test_exit_button.cpp \
	test_hostname.cpp \
	test_line_buffer.cpp \
	test_mode.cpp \
	test_ota.cpp \
	test_poll.cpp \
	test_provision.cpp \
	test_redeem.cpp \
	test_scan.cpp \
	test_service_button.cpp \
	test_status_led.cpp \
	test_time.cpp \
	$(wildcard $(CORE_DIR)/src/*.cpp)

.PHONY: all test clean

all: test

test: $(BINARY)
	$(BINARY)

$(BINARY): $(SOURCES) check.hpp $(wildcard $(CORE_DIR)/include/viaaccess/*.hpp)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(CORE_DIR)/include -I. $(SOURCES) -o $@

clean:
	rm -rf $(BUILD_DIR)
