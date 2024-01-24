#
# This source code is released into the public domain.
#

BINDIR	?= bin

default: all

all: ${TARGET}

OBJS	= ${SRCS:S/.c$/.o/:S/.cc$/.o/}
MODOBJS	= ${MODULES:S/$/.o/}
VPATH	+= ${TOPDIR}/modules

${TARGET}: ${OBJS} ${MODOBJS}
	${CXX} ${CFLAGS} ${LDFLAGS} -o ${TARGET} $> ${LIBS}

depend:
.for src in ${SRCS}
	echo >> ${_COMPDB} '{'
	echo >> ${_COMPDB} '"directory": "${.CURDIR}",'
	echo >> ${_COMPDB} '"command": "${CXX} ${CPPFLAGS} ${CXXFLAGS} ${.CURDIR}/${src} -c -o ${src:R}.o -MMD -MF ${.OBJDIR}/${src}.d",'
	echo >> ${_COMPDB} '"file": "${.CURDIR}/${src}",'
	echo >> ${_COMPDB} '"output": "${src:R}.o"'
	echo >> ${_COMPDB} '},'
.endfor


clean:
	rm -f ${TARGET} ${OBJS}

install: ${TARGET}
	install -d "${PREFIX}/${BINDIR}"
	install -C ${TARGET} "${PREFIX}/${BINDIR}"

.include "${.PARSEDIR}/vars.mk"
.include "${.PARSEDIR}/rules.mk"
.dinclude "${.OBJDIR}/.modules.depend"
.for src in ${SRCS}
.dinclude "${.OBJDIR}/${src:R:S/$/.d/}"
.endfor
