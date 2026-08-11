// BOOT button gesture classification.
#include "check.hpp"
#include "viaaccess/service_button.hpp"

using viaaccess::ServiceButtonConfig;
using viaaccess::ServiceButtonEngine;
using viaaccess::ServiceGesture;

VA_TEST(ServiceButtonSingleClick) {
  ServiceButtonEngine engine;
  ServiceButtonConfig cfg;
  cfg.debounce_ms = 0;
  cfg.click_window_ms = 200;
  engine.Reset(cfg);

  CHECK(engine.ApplyRaw(false, 0) == ServiceGesture::kNone);
  CHECK(engine.ApplyRaw(true, 10) == ServiceGesture::kNone);
  CHECK(engine.ApplyRaw(false, 50) == ServiceGesture::kNone);
  CHECK(engine.ApplyRaw(false, 260) == ServiceGesture::kSingleClick);
}

VA_TEST(ServiceButtonDoubleClick) {
  ServiceButtonEngine engine;
  ServiceButtonConfig cfg;
  cfg.debounce_ms = 0;
  cfg.click_window_ms = 300;
  engine.Reset(cfg);

  engine.ApplyRaw(false, 0);
  engine.ApplyRaw(true, 10);
  engine.ApplyRaw(false, 40);
  engine.ApplyRaw(true, 80);
  engine.ApplyRaw(false, 120);
  CHECK(engine.ApplyRaw(false, 450) == ServiceGesture::kDoubleClick);
}

VA_TEST(ServiceButtonTripleClickFiresImmediately) {
  ServiceButtonEngine engine;
  ServiceButtonConfig cfg;
  cfg.debounce_ms = 0;
  cfg.click_window_ms = 400;
  engine.Reset(cfg);

  engine.ApplyRaw(false, 0);
  engine.ApplyRaw(true, 10);
  engine.ApplyRaw(false, 40);
  engine.ApplyRaw(true, 80);
  engine.ApplyRaw(false, 120);
  engine.ApplyRaw(true, 160);
  CHECK(engine.ApplyRaw(false, 200) == ServiceGesture::kTripleClick);
}

VA_TEST(ServiceButtonLongPressArmedThenCommit) {
  ServiceButtonEngine engine;
  ServiceButtonConfig cfg;
  cfg.debounce_ms = 0;
  cfg.long_press_arm_ms = 1000;
  cfg.long_press_ms = 2500;
  engine.Reset(cfg);

  engine.ApplyRaw(false, 0);
  CHECK(engine.ApplyRaw(true, 10) == ServiceGesture::kNone);
  CHECK(engine.ApplyRaw(true, 1010) == ServiceGesture::kLongPressArmed);
  CHECK(engine.ApplyRaw(true, 1500) == ServiceGesture::kNone);
  CHECK(engine.ApplyRaw(true, 2510) == ServiceGesture::kLongPress);
  // Release after long press must not become a click.
  CHECK(engine.ApplyRaw(false, 2600) == ServiceGesture::kNone);
  CHECK(engine.ApplyRaw(false, 3200) == ServiceGesture::kNone);
}

VA_TEST(ServiceButtonLongPressCancelsClickWindow) {
  ServiceButtonEngine engine;
  ServiceButtonConfig cfg;
  cfg.debounce_ms = 0;
  cfg.click_window_ms = 400;
  cfg.long_press_arm_ms = 500;
  cfg.long_press_ms = 1000;
  engine.Reset(cfg);

  engine.ApplyRaw(false, 0);
  engine.ApplyRaw(true, 10);
  engine.ApplyRaw(false, 40);  // one short click pending
  engine.ApplyRaw(true, 80);   // second press becomes a hold
  CHECK(engine.ApplyRaw(true, 600) == ServiceGesture::kLongPressArmed);
  CHECK(engine.ApplyRaw(true, 1100) == ServiceGesture::kLongPress);
}
