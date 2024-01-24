#
# This source code is released into the public domain.
#

OBJS=	${SRCS:R:S/$/.o/}

default: all
all: ${OBJS}

depend:
.for src in ${SRCS}
	echo >> ${_COMPDB} '{'
	echo >> ${_COMPDB} '"directory": "${.CURDIR}",'
	echo >> ${_COMPDB} '"command": "${CXX} ${CPPFLAGS} ${CXXFLAGS} ${.CURDIR}/${src} -c -o ${.OBJDIR}/${src:R}.o -MMD -MF ${.OBJDIR}/${src}.d",'
	echo >> ${_COMPDB} '"file": "${.CURDIR}/${src}",'
	echo >> ${_COMPDB} '"output": "${.OBJDIR}/${src:R}.o"'
	echo >> ${_COMPDB} '},'
.endfor


clean:
	rm -f ${TARGET} ${OBJS}

install: ${TARGET}
	install -d "${PREFIX}/${BINDIR}"
	install -C ${TARGET} "${PREFIX}/${BINDIR}"

.include "${.PARSEDIR}/vars.mk"
.include "${.PARSEDIR}/rules.mk"
.for src in ${SRCS}
.include "${src:S/$/.d/}"
.endfor
