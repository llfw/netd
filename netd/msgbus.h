/*
 * This source code is released into the public domain.
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

typedef void (*msgbus_handler_t)(msg_id_t, void *);

/* initialise the msgbus */
int	msgbus_init(void);

/* post a message to the bus */
void	msgbus_post(msg_id_t, void *);

/* subscribe to a message type */
void	msgbus_sub(msg_id_t, msgbus_handler_t);

#endif	/* !NETD_MSGBUS_H_INCLUDED */
