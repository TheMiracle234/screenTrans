// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <cstdint>
#include <util/compile_time.hpp>
#include <net/tcp/Client.hpp>
#include <Room.h>

// We can use enum and define a bunch of functions which is small in size to support a more modern mode, but I reject. It's a stupied waste in most aspects.

#define DECL_CONST constexpr inline

using Signal = int8_t;
DECL_CONST Signal signal_none			= 0;
DECL_CONST Signal signal_closed			= Signal(1) << 0;
DECL_CONST Signal signal_choiceNotMatch	= Signal(1) << 1;
//DECL_CONST Signal signal_littleEnd		= Signal(1) << 2;

using Choice = int;
DECL_CONST Choice choice_invalid	= -1;
DECL_CONST Choice choice_enter_room	= 0;
DECL_CONST Choice choice_make_room	= 1;

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


DECL_CONST std::size_t words_msg_bytes = 256;
DECL_CONST std::size_t name_bytes = 128;


struct s_HostPower {
	ALIGN(bool) is_host_from_client;
	ALIGN(bool) allow_asr;
	ALIGN(bool) allow_translator;
	ALIGN(uint32_t) set_passwd;
	ALIGN(user::id_t) wanna_kick_off;
	ALIGN(user::id_t) transfer_host;
};

struct s_Msg {
	ALIGN(Signal) signal;

	ALIGN(net::End) end;
	ALIGN(user::id_t) id;
	struct {
		ALIGN(char) buf[name_bytes];
	} name;
	ALIGN(bool) is_host_from_server;
	//s_HostPower hostPower;
	ALIGN(bool) is_host_from_client;
	ALIGN(bool) allow_asr;
	ALIGN(bool) allow_translator;
	//ALIGN(uint32_t) set_passwd;
	//union alignas(sizeof(uint32_t)) {
	//	ALIGN(uint32_t) set_passwd;
		ALIGN(uint32_t) passwd;
	//};
	ALIGN(user::id_t) wanna_kick_off;
	ALIGN(user::id_t) transfer_host;

	
	ALIGN(bool) has_words_msg;
	ALIGN(user::id_t) words_msg_target;
	struct {
		ALIGN(char) buf[words_msg_bytes];
	} words_msg;
	//ALIGN(char) words_msg_buf[words_msg_bytes];
	//ALIGN(std::uint64_t) chosen_users_count;

	ALIGN(bool) share_video;
	ALIGN(std::int32_t) sc_w;
	ALIGN(std::int32_t) sc_h;
	ALIGN(std::int32_t) pk_size;

	ALIGN(bool) share_camera;
	ALIGN(std::int32_t) cam_w;
	ALIGN(std::int32_t) cam_h;
	ALIGN(std::int32_t) cam_pk_size;

};

static_assert(sizeof(bool) == 1);

inline void s_MsgHton(s_Msg& msg) {
    msg.signal           = net::host_to_network(msg.signal);
    msg.end              = net::host_to_network(msg.end);
    msg.id               = net::host_to_network(msg.id);
    // msg.name.buf      : char 数组，无需转换
    // msg.is_host_from_server, is_host_from_client, allow_asr, allow_translator : bool，无需转换
    msg.passwd           = net::host_to_network(msg.passwd);
    msg.wanna_kick_off   = net::host_to_network(msg.wanna_kick_off);
    msg.transfer_host    = net::host_to_network(msg.transfer_host);
    msg.has_words_msg    ; // bool，无需转换
    msg.words_msg_target = net::host_to_network(msg.words_msg_target);
    // msg.words_msg.buf : char 数组，无需转换
    msg.share_video      ; // bool，无需转换
    msg.sc_w             = net::host_to_network(msg.sc_w);
    msg.sc_h             = net::host_to_network(msg.sc_h);
    msg.pk_size          = net::host_to_network(msg.pk_size);
    msg.share_camera     ; // bool，无需转换
    msg.cam_w            = net::host_to_network(msg.cam_w);
    msg.cam_h            = net::host_to_network(msg.cam_h);
    msg.cam_pk_size      = net::host_to_network(msg.cam_pk_size);
}

inline void s_MsgNtoh(s_Msg& msg) {
    msg.signal           = net::network_to_host(msg.signal);
    msg.end              = net::network_to_host(msg.end);
    msg.id               = net::network_to_host(msg.id);
    msg.passwd           = net::network_to_host(msg.passwd);
    msg.wanna_kick_off   = net::network_to_host(msg.wanna_kick_off);
    msg.transfer_host    = net::network_to_host(msg.transfer_host);
    msg.words_msg_target = net::network_to_host(msg.words_msg_target);
    msg.sc_w             = net::network_to_host(msg.sc_w);
    msg.sc_h             = net::network_to_host(msg.sc_h);
    msg.pk_size          = net::network_to_host(msg.pk_size);
    msg.cam_w            = net::network_to_host(msg.cam_w);
    msg.cam_h            = net::network_to_host(msg.cam_h);
    msg.cam_pk_size      = net::network_to_host(msg.cam_pk_size);
}

//struct s_HostPower_r {
//	ALIGN(bool) is_host_from_client;
//	ALIGN(bool) allow_asr;
//	ALIGN(bool) allow_translator;
//	ALIGN(user::id_t) wanna_kick_off;
//};

#undef DECL_CONST