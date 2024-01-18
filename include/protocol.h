/*
 * This source code is released into the public domain.
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

#define	CTL_CMD_NAME		"CMD_NAME"
#define	CTL_CMD_LIST_INTERFACES	"INTF_LIST"

#define	CTL_PARM_INTERFACES	"INTFS"
#define	CTL_PARM_INTERFACE_NAME	"NAME"

#endif	/* !NETD_PROTOCOL_H_INCLUDED */
