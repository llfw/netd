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

#ifndef	NETD_MSGBUS_H_INCLUDED
#define	NETD_MSGBUS_H_INCLUDED

/*
 * msgbus is an internal message bus used to distribute events.
 */

/* msg id: 16 bits category, 16 bits type */
typedef uint32_t msg_id_t;
#define	MSG_ID(category, type)	(((msg_id_t)(category) << 16) 	\
				 | ((msg_id_t)(type) & 0xFFFF))
#define	MSG_CATEGORY(id)	((id) >> 16)
#define	MSG_TYPE(id)		((id) & 0xFFFF)

/* allocated categories - keep here to avoid accidental duplicates */
#define	MSG_C_NETLINK		0x1
#define	MSG_C_STATE		0x2

typedef void (*msgbus_handler_t)(msg_id_t, void * nullable);

/* initialise the msgbus */
int	msgbus_init(void);

/* post a message to the bus */
void	msgbus_post(msg_id_t, void * nullable);

/* subscribe to a message type */
void	msgbus_sub(msg_id_t, msgbus_handler_t nonnull);

#endif	/* !NETD_MSGBUS_H_INCLUDED */
