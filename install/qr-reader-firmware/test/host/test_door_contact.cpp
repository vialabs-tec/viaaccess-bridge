// Door-contact debounce and held_open, matching the Go agent's service tests.
#include "check.hpp"
#include "viaaccess/door_contact.hpp"

using viaaccess::DoorContactEngine;
using viaaccess::DoorKind;
using viaaccess::DoorState;

VA_TEST(DoorContactDebouncesOpenAndClose) {
  DoorContactEngine engine;
  engine.Reset(30, 60000);
  engine.Seed(false, 0);
  CHECK(engine.stable() == DoorState::kClosed);

  auto step = engine.ApplyRaw(true, 100);
  CHECK_EQ(step.count, 0);
  CHECK(engine.stable() == DoorState::kClosed);

  step = engine.Tick(129);
  CHECK_EQ(step.count, 0);

  step = engine.Tick(130);
  CHECK_EQ(step.count, 1);
  CHECK(step.kinds[0] == DoorKind::kOpened);
  CHECK(engine.stable() == DoorState::kOpen);

  step = engine.ApplyRaw(false, 200);
  CHECK_EQ(step.count, 0);
  step = engine.Tick(230);
  CHECK_EQ(step.count, 1);
  CHECK(step.kinds[0] == DoorKind::kClosed);
  CHECK(engine.stable() == DoorState::kClosed);
}

VA_TEST(DoorContactHeldOpenFiresOnce) {
  DoorContactEngine engine;
  engine.Reset(10, 40);
  engine.Seed(false, 0);

  engine.ApplyRaw(true, 0);
  auto step = engine.Tick(10);
  CHECK_EQ(step.count, 1);
  CHECK(step.kinds[0] == DoorKind::kOpened);

  step = engine.Tick(49);
  CHECK_EQ(step.count, 0);

  step = engine.Tick(50);
  CHECK_EQ(step.count, 1);
  CHECK(step.kinds[0] == DoorKind::kHeldOpen);

  step = engine.Tick(100);
  CHECK_EQ(step.count, 0);
}

VA_TEST(DoorContactSeedDoesNotEmit) {
  DoorContactEngine engine;
  engine.Reset(10, 1000);
  engine.Seed(true, 0);
  CHECK(engine.stable() == DoorState::kOpen);

  // Same reading after seed must stay quiet; only a change emits.
  auto step = engine.ApplyRaw(true, 10);
  CHECK_EQ(step.count, 0);
  step = engine.Tick(10);
  CHECK_EQ(step.count, 0);
}

VA_TEST(DoorContactCloseCancelsHeldOpen) {
  DoorContactEngine engine;
  engine.Reset(10, 100);
  engine.Seed(false, 0);

  engine.ApplyRaw(true, 0);
  auto step = engine.Tick(10);
  CHECK_EQ(step.count, 1);
  CHECK(step.kinds[0] == DoorKind::kOpened);

  engine.ApplyRaw(false, 20);
  step = engine.Tick(30);
  CHECK_EQ(step.count, 1);
  CHECK(step.kinds[0] == DoorKind::kClosed);

  step = engine.Tick(200);
  CHECK_EQ(step.count, 0);
}

VA_TEST(DoorKindStringsMatchIdentityWireFormat) {
  CHECK_EQ(std::string(viaaccess::DoorKindString(DoorKind::kOpened)), "opened");
  CHECK_EQ(std::string(viaaccess::DoorKindString(DoorKind::kClosed)), "closed");
  CHECK_EQ(std::string(viaaccess::DoorKindString(DoorKind::kHeldOpen)), "held_open");
  CHECK_EQ(std::string(viaaccess::DoorStateString(DoorState::kOpen)), "open");
  CHECK_EQ(std::string(viaaccess::DoorStateString(DoorState::kClosed)), "closed");
}
