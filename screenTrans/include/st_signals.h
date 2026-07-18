/*
    screenTrans - online meeting app
    Copyright (C) 2026 Yuan Aowei

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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