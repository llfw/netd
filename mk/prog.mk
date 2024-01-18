#
# This source code is released into the public domain.
#

BINDIR	?= bin

default: all

all: ${TARGET}

OBJS=	${SRCS:.c=.o}

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c $<

${TARGET}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${TARGET} ${OBJS} ${LIBS}

depend:
	mkdep ${CPPFLAGS} ${CFLAGS} ${SRCS}

clean:
	rm -f ${TARGET} ${OBJS}

install: ${TARGET}
	install -d "${PREFIX}/${BINDIR}"
	install -C ${TARGET} "${PREFIX}/${BINDIR}"

.include "${.PARSEDIR}/vars.mk"

.dinclude ".depend"
