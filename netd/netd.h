/*
 * This source code is released into the public domain.
 */

#ifndef NETD_NETD_H
#define NETD_NETD_H

#include	<stdarg.h>
#include	<paths.h>

#define	ASIZE(a)	(sizeof(a) / sizeof(*(a)))

/* log a message at the highest priority and immediately abort */
_Noreturn void	vpanic(char const *str, va_list args);
_Noreturn void	panic(char const *str, ...);

#endif	/* !NETD_NETD_H */