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

#ifndef	NETD_LOG_H_INCLUDED
#define	NETD_LOG_H_INCLUDED

/*
 * message logging (syslog and stderr).
 */

#include	<stdarg.h>

#include	"defs.h"

typedef enum {
	NLOG_DEBUG,
	NLOG_INFO,
	NLOG_WARNING,
	NLOG_ERROR,
	NLOG_FATAL,
} loglevel_t;

/* log a message at the given level */
void		nlog(loglevel_t, char const *message, ...) printf_format(2, 3);
void		vnlog(loglevel_t, char const *message, va_list);

/* get or set the log destination */
#define	NLOG_SYSLOG	0x1u
#define	NLOG_STDERR	0x2u
#define NLOG_MASK	0x3u
#define	NLOG_DEFAULT	NLOG_STDERR
unsigned	nlog_getdest(void);
void		nlog_setdest(unsigned);

#endif	/* !NETD_LOG_H_INCLUDED */
