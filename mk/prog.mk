#
# This source code is released into the public domain.
#

default: build
build: ${TARGET}

OBJS=	${SRCS:.c=.o}

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c $<

${TARGET}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${TARGET} ${OBJS} ${LIBS}

depend:
	mkdep ${CPPFLAGS} ${CFLAGS} ${SRCS}

clean:
	rm -f ${TARGET} ${OBJS}

.include "${.PARSEDIR}/vars.mk"

.dinclude ".depend"
