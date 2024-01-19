/*
 * This source code is released into the public domain.
 */

#ifndef	NETD_DB_H_INCLUDED
#define	NETD_DB_H_INCLUDED

/*
 * the persistent database.
 */

#include	<sys/types.h>
#include	<sys/uuid.h>

#include	<netinet/if_ether.h>

/* 
 * a stored interface.  interfaces are identified primarily by UUID, and are
 * matched to live interfaces using MAC address or name, depending on
 * configuration.
 */

typedef struct pinterface {
	struct uuid		 pi_uuid;
	char const		*pi_name;
	char const		*pi_descr;
	struct ether_addr	 pi_ether;
} pinterface_t;

#endif	/* !NETD_DB_H_INCLUDED */
