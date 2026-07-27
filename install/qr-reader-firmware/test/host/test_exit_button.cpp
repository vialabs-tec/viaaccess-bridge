// Exit-button debounce, arming and cooldown — same cases as the Go agent.
#include "check.hpp"
#include "viaaccess/exit_button.hpp"

using viaaccess::ExitButtonEngine;
using viaaccess::ExitButtonState;

VA_TEST(ExitButtonPressEmitsOnceAndCooldown) {
  ExitButtonEngine engine;
  engine.Reset(0, 500);
  engine.Seed(false, 0);
  CHECK(engine.stable() == ExitButtonState::kIdle);

  auto step = engine.ApplyRaw(true, 100);
  CHECK(step.emit_pressed);
  CHECK(engine.stable() == ExitButtonState::kPressed);

  // Held: no second fire.
  step = engine.ApplyRaw(true, 150);
  CHECK(!step.emit_pressed);

  step = engine.ApplyRaw(false, 200);
  CHECK(!step.emit_pressed);
  CHECK(engine.stable() == ExitButtonState::kIdle);

  // Still in cooldown.
  step = engine.ApplyRaw(true, 300);
  CHECK(!step.emit_pressed);
  CHECK(engine.stable() == ExitButtonState::kPressed);

  step = engine.ApplyRaw(false, 350);
  CHECK(!step.emit_pressed);

  // Cooldown elapsed.
  step = engine.ApplyRaw(true, 700);
  CHECK(step.emit_pressed);
}

VA_TEST(ExitButtonSeedStuckDoesNotEmit) {
  ExitButtonEngine engine;
  engine.Reset(0, 3000);
  engine.Seed(true, 0);
  CHECK(engine.stable() == ExitButtonState::kPressed);

  auto step = engine.ApplyRaw(true, 10);
  CHECK(!step.emit_pressed);

  // Must release before the first unlock.
  step = engine.ApplyRaw(false, 20);
  CHECK(!step.emit_pressed);
  step = engine.ApplyRaw(true, 30);
  CHECK(step.emit_pressed);
}

VA_TEST(ExitButtonDebounceWaitsBeforePress) {
  ExitButtonEngine engine;
  engine.Reset(30, 3000);
  engine.Seed(false, 0);

  auto step = engine.ApplyRaw(true, 100);
  CHECK(!step.emit_pressed);
  step = engine.Tick(129);
  CHECK(!step.emit_pressed);
  step = engine.Tick(130);
  CHECK(step.emit_pressed);
  CHECK(engine.stable() == ExitButtonState::kPressed);
}

VA_TEST(ExitButtonStateStrings) {
  CHECK_EQ(std::string(viaaccess::ExitButtonStateString(ExitButtonState::kIdle)), "idle");
  CHECK_EQ(std::string(viaaccess::ExitButtonStateString(ExitButtonState::kPressed)),
           "pressed");
  CHECK_EQ(std::string(viaaccess::ExitButtonStateString(ExitButtonState::kUnknown)),
           "unknown");
}
