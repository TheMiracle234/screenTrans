#pragma once

#include <cstdint>

using Signal = uint8_t;

constexpr inline Signal SignalBit(int n) { return (Signal)1 << n; }

constexpr inline Signal signal_none				= 0;
constexpr inline Signal signal_closed			= SignalBit(0);
constexpr inline Signal signal_choiceNotMatch	= SignalBit(1);