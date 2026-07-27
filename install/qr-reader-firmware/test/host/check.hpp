// Minimal assertion and registration helpers for the host test target.
//
// The firmware has no unit test framework dependency on purpose: these tests
// build the viaaccess_core sources with a plain compiler so the ported logic
// can be checked on a laptop, without an ESP-IDF install or a board attached.
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace vatest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

std::vector<TestCase>& Registry();

struct Registrar {
  Registrar(const char* name, std::function<void()> fn);
};

[[noreturn]] void Fail(const char* file, int line, const std::string& message);

int RunAll();

inline std::string ToText(const std::string& value) { return "\"" + value + "\""; }
inline std::string ToText(const char* value) { return ToText(std::string(value)); }
inline std::string ToText(bool value) { return value ? "true" : "false"; }

template <typename T>
std::string ToText(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

}  // namespace vatest

#define VA_TEST(test_name)                                                  \
  static void test_name();                                                  \
  static ::vatest::Registrar vatest_registrar_##test_name(#test_name,       \
                                                          test_name);       \
  static void test_name()

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      ::vatest::Fail(__FILE__, __LINE__, "CHECK failed: " #condition);      \
    }                                                                       \
  } while (false)

#define CHECK_EQ(actual, expected)                                          \
  do {                                                                      \
    const auto& va_actual = (actual);                                       \
    const auto& va_expected = (expected);                                   \
    if (!(va_actual == va_expected)) {                                      \
      ::vatest::Fail(__FILE__, __LINE__,                                    \
                     std::string(#actual) + " = " +                         \
                         ::vatest::ToText(va_actual) + ", want " +          \
                         ::vatest::ToText(va_expected));                    \
    }                                                                       \
  } while (false)
