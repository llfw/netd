/*
 * This source code is released into the public domain.
 */

#ifndef	NETD_KQ_H_INCLUDED
#define	NETD_KQ_H_INCLUDED

typedef enum {
	KQ_REARM,
	KQ_STOP,
} kq_disposition_t;

struct kq;

typedef kq_disposition_t (*kq_read_handler) (struct kq *, int fd, void *udata);

struct kq	*kq_create		(void);
int		 kq_register_read	(struct kq *, int fd,
					 kq_read_handler reader, void *udata);
int		 kq_run			(struct kq *);

extern time_t current_time;

#endif	/* !NETD_KQ_H_INCLUDED */
