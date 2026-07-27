#include "check.hpp"

#include <cstdio>
#include <stdexcept>

namespace vatest {
namespace {

struct TestFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

}  // namespace

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

Registrar::Registrar(const char* name, std::function<void()> fn) {
  Registry().push_back(TestCase{name, std::move(fn)});
}

void Fail(const char* file, int line, const std::string& message) {
  throw TestFailure(std::string(file) + ":" + std::to_string(line) + ": " + message);
}

int RunAll() {
  int failed = 0;
  for (const TestCase& test : Registry()) {
    try {
      test.fn();
      std::printf("ok   %s\n", test.name.c_str());
    } catch (const std::exception& err) {
      std::printf("FAIL %s\n     %s\n", test.name.c_str(), err.what());
      ++failed;
    }
  }
  std::printf("\n%zu tests, %d failed\n", Registry().size(), failed);
  return failed == 0 ? 0 : 1;
}

}  // namespace vatest

int main() { return vatest::RunAll(); }
