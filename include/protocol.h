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

#include	<paths.h>

#define	CTL_SOCKET_PATH	_PATH_VARRUN "netd.sock"

/* 
 * we should consider removing this limit for people with thousands of
 * interfaces or whatever.
 */
#define CTL_MAX_MSG_SIZE	4096

/*
 * protocol constants for nvlist keys.
 */

#define	CP_CMD			"CMD_NAME"

/* INTF_LIST command - request */
#define	CC_GETIFS		"INTF_LIST"

/* INTF_LIST command - response */
#define	CP_IFACE		"INTFS"		/* nvlist array */
#define	CP_IFACE_NAME		"NAME"		/* string */
#define CP_IFACE_FLAGS		"FLAGS"		/* string array */
#define CP_IFACE_ADMIN		"ADMIN_STATE"	/* number */
#define CV_IFACE_ADMIN_UNKNOWN		0
#define CV_IFACE_ADMIN_DOWN		1
#define CV_IFACE_ADMIN_UP		2
#define CP_IFACE_OPER		"OPER_STATE"	/* number */
#define CV_IFACE_OPER_UNKNOWN		0
#define CV_IFACE_OPER_NOT_PRESENT	1
#define CV_IFACE_OPER_DOWN		2
#define CV_IFACE_OPER_LOWER_DOWN	3
#define CV_IFACE_OPER_TESTING		4
#define CV_IFACE_OPER_DORMANT		5
#define CV_IFACE_OPER_UP		6
#define	CP_IFACE_RXRATE		"RX"		/* number (bits/sec) */
#define	CP_IFACE_TXRATE		"TX"		/* number (bits/sec) */

#endif	/* !NETD_PROTOCOL_H_INCLUDED */
