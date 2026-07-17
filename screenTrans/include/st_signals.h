#pragma once

#include <cstdint>

// We can use enum and define a bunch of functions which is small in size to support a more modern mode, but I reject. It's a stupied waste in most aspects.

using Signal = int8_t;
constexpr inline Signal signal_none				= 0;
constexpr inline Signal signal_closed			= Signal(1) << 0;
constexpr inline Signal signal_choiceNotMatch	= Signal(1) << 1;

using Choice = int;
constexpr inline Choice choice_invalid		= -1;
constexpr inline Choice choice_enter_room	= 0;
constexpr inline Choice choice_make_room	= 1;