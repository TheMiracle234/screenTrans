// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <cstdint>
#include <util/compile_time.hpp>
#include <Room.h>

// We can use enum and define a bunch of functions which is small in size to support a more modern mode, but I reject. It's a stupied waste in most aspects.

#define DECL_CONST constexpr inline

using Signal = int8_t;
DECL_CONST Signal signal_none			= 0;
DECL_CONST Signal signal_closed			= Signal(1) << 0;
DECL_CONST Signal signal_choiceNotMatch	= Signal(1) << 1;
DECL_CONST Signal signal_littleEnd		= Signal(1) << 2;


using Choice = int;
DECL_CONST Choice choice_invalid	= -1;
DECL_CONST Choice choice_enter_room	= 0;
DECL_CONST Choice choice_make_room	= 1;

DECL_CONST size_t words_msg_bytes = 256;

using Connect_t = uint8_t;
DECL_CONST Connect_t connect_user	= 0;
DECL_CONST Connect_t connect_audio	= 1;

using faudio_t = uint8_t;
DECL_CONST faudio_t faudio_none			= 0;
DECL_CONST faudio_t faudio_share			= faudio_t(1) << 0;
DECL_CONST faudio_t faudio_mute_all_once	= faudio_t(1) << 1;
DECL_CONST faudio_t faudio_mute_all_keep	= faudio_t(1) << 2;
DECL_CONST faudio_t faudio_is_host			= faudio_t(1) << 3;
DECL_CONST faudio_t faudio_little_end		= faudio_t(1) << 4;


DECL_CONST std::size_t name_bytes = 128;

struct s_HostPower {
	ALIGN(bool) is_host_from_client;
	ALIGN(bool) allow_asr;
	ALIGN(bool) allow_translator;
	ALIGN(uint32_t) set_passwd;
	ALIGN(user::id_t) wanna_kick_off;
	ALIGN(user::id_t) transfer_host;
};

//struct s_HostPower_r {
//	ALIGN(bool) is_host_from_client;
//	ALIGN(bool) allow_asr;
//	ALIGN(bool) allow_translator;
//	ALIGN(user::id_t) wanna_kick_off;
//};

#undef DECL_CONST