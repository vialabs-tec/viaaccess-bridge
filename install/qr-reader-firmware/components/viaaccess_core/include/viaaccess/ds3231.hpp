// DS3231 register encoding, kept away from the I2C driver so it is unit tested.
//
// This is where the bugs live in an RTC integration: BCD packing, the 12/24 hour
// flag a previous Arduino sketch may have left behind, the century bit, and the
// oscillator stop flag that tells a dead coin cell apart from a valid reading.
#pragma once

#include <cstdint>

namespace viaaccess {

inline constexpr uint8_t kDs3231Address = 0x68;
inline constexpr uint8_t kDs3231RegTime = 0x00;
inline constexpr uint8_t kDs3231RegControl = 0x0E;
inline constexpr uint8_t kDs3231RegStatus = 0x0F;
inline constexpr uint8_t kDs3231RegTemperature = 0x11;
inline constexpr int kDs3231TimeRegCount = 7;

// OscillatorStopped reads the OSF bit of the status register. It is set whenever
// the oscillator was interrupted, which on this module means the cell died or
// the time was never written: the registers may look sane and still be wrong.
bool Ds3231OscillatorStopped(uint8_t status);

// ClearOscillatorStopFlag returns the status byte to write back after setting
// the time, preserving the alarm flags the module may be using.
uint8_t Ds3231ClearOscillatorStopFlag(uint8_t status);

// DecodeDs3231Time converts the seven timekeeping registers to Unix seconds, or
// 0 when any field is out of range. Both 24 hour and 12 hour modes are accepted
// because the module may arrive configured by whatever used it last.
int64_t DecodeDs3231Time(const uint8_t* regs);

// EncodeDs3231Time fills the seven registers in 24 hour mode. Returns false when
// the timestamp is outside the range the chip can hold (2000 to 2199).
bool EncodeDs3231Time(int64_t unix_seconds, uint8_t* regs);

// DecodeDs3231Temperature converts registers 0x11 and 0x12 to Celsius, in 0.25
// steps. Reported in /health so a reader cooking inside a sealed enclosure is
// visible from the dashboard.
double DecodeDs3231Temperature(uint8_t msb, uint8_t lsb);

}  // namespace viaaccess
