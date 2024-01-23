/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to the
 * public domain. We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors. We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all present
 * and future rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef	NETD_PROTOCOL_H_INCLUDED
#define	NETD_PROTOCOL_H_INCLUDED

/*
 * the client-server protocol.
 */

#include	<string>

#include	<paths.h>

namespace proto {

using namespace std::literals;

constexpr std::string_view socket_path = _PATH_VARRUN "netd.sock";

/*
 * we should consider removing this limit for people with thousands of
 * interfaces or whatever.
 */
constexpr std::size_t max_msg_size = 4096;

/*
 * maximum length of a network name.  these are supposed to be short names
 * similar to interface names (like "net0"), so limit to 16 characters for now.
 * there's no technical reason this couldn't be raised in future if needed.
 */

constexpr auto cn_maxnetnam = 16u;

/*
 * protocol constants for nvlist keys.
 */

constexpr std::string_view
	cp_cmd			= "CMD_NAME"sv,

	/* generic response */
	cp_status		= "STATUS"sv,		/* string */
	cv_status_success		= "STATUS_SUCCESS"sv,
	cv_status_error			= "STATUS_ERROR"sv,
	cp_status_info		= "STATUS_INFO"sv,	/* string, optional */
	cp_status_syserr	= "STATUS_SYSERR"sv,	/* string */

	/*
	 * error codes
	 */
	ce_syserr	= "SYSERR"sv,		/* system error,
						   see STATUS_SYSERR */
	ce_proto	= "PROTO"sv,		/* protocol error */
	ce_netnx	= "NETNX"sv,		/* network does not exist */
	ce_netexists	= "NETEXISTS"sv,	/* network already exists */
	ce_netnmln	= "NETNMLN"sv;		/* network name is too long */

	/*
	 * interface-related commands.
	 */

	/* INTF_LIST - request */
constexpr std::string_view
	cc_getifs	= "INTF_LIST",

	/* INTF_LIST - response */
	cp_iface		= "INTFS"sv,		/* nvlist array */
	cp_iface_name		= "NAME"sv,		/* string */
	cp_iface_flags		= "FLAGS"sv,		/* string array */
	cp_iface_admin		= "ADMIN_STATE",	/* number */
	cp_iface_oper		= "OPER_STATE",		/* number */
	cp_iface_rxrate		= "RX",			/* number (bits/sec) */
	cp_iface_txrate		= "TX";			/* number (bits/sec) */

constexpr int
	/* interface operational states */
	cv_iface_oper_unknown		= 0,
	cv_iface_oper_not_present	= 1,
	cv_iface_oper_down		= 2,
	cv_iface_oper_lower_down	= 3,
	cv_iface_oper_testing		= 4,
	cv_iface_oper_dormant		= 5,
	cv_iface_oper_up		= 6,
	/* interface admin states */
	cv_iface_admin_unknown		= 0,
	cv_iface_admin_down		= 1,
	cv_iface_admin_up		= 2;

	/*
	 * network-related commands.
	 */

constexpr std::string_view
	/* NET_LIST - request */
	cc_getnets		= "NET_LIST",

	/* NET_LIST - response */
	cp_nets			= "NETS",
	cp_net_name		= "NAME",

	/* NET_CREATE - request */
	cc_newnet		= "NET_CREATE",
	cp_newnet_name		= "NET_NAME",

	/* NET_DELETE - request */
	cc_delnet		= "NET_DELETE",
	cp_delnet_name		= "NET_NAME";

} // namespace proto

#endif	/* !NETD_PROTOCOL_H_INCLUDED */
