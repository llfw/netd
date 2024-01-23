#
# This source code is released into the public domain.
#

BINDIR	?= bin

default: all

all: ${TARGET}

OBJS=	${SRCS:S/.c$/.o/:S/.cc$/.o/}

${TARGET}: ${OBJS}
	${CXX} ${CFLAGS} ${LDFLAGS} -o ${TARGET} ${OBJS} ${LIBS}

depend:
	mkdep ${CPPFLAGS} ${CXXFLAGS} ${SRCS}

clean:
	rm -f ${TARGET} ${OBJS}

install: ${TARGET}
	install -d "${PREFIX}/${BINDIR}"
	install -C ${TARGET} "${PREFIX}/${BINDIR}"

.include "${.PARSEDIR}/vars.mk"
.include "${.PARSEDIR}/rules.mk"

.dinclude ".depend"
