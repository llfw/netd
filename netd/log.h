/*
 * This source code is released into the public domain.
 */

#ifndef	NETD_LOG_H_INCLUDED
#define	NETD_LOG_H_INCLUDED

/*
 * message logging (syslog and stderr).
 */

#include	<stdarg.h>

typedef enum {
	NLOG_DEBUG,
	NLOG_INFO,
	NLOG_WARNING,
	NLOG_ERROR,
	NLOG_FATAL,
} loglevel_t;

/* log a message at the given level */
void		nlog(loglevel_t, char const *message, ...);
void		vnlog(loglevel_t, char const *message, va_list);

/* get or set the log destination */
#define	NLOG_SYSLOG	0x1
#define	NLOG_STDERR	0x2
#define NLOG_MASK	0x3
#define	NLOG_DEFAULT	NLOG_STDERR
unsigned	nlog_getdest(void);
void		nlog_setdest(unsigned);

#endif	/* !NETD_LOG_H_INCLUDED */
