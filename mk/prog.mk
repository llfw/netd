#
# This source code is released into the public domain.
#

BINDIR	?= bin

default: all

all: ${TARGET}

OBJS	= ${SRCS:S/.cc$/.o/:S/.ccm/.module.o/}
MODOBJS	= ${MODULES:S/$/.module.o/}
VPATH	+= ${TOPDIR}/modules

${TARGET}: ${OBJS} ${MODOBJS}
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o ${TARGET} $> ${LIBS}

depend:
.for src in ${SRCS:M*.ccm}
	echo >> ${_COMPDB} '{'
	echo >> ${_COMPDB} '"directory": "${.CURDIR}",'
	echo >> ${_COMPDB} '"command": "${CXX} --precompile ${CPPFLAGS} ${CXXFLAGS} ${.CURDIR}/${src} -c -o ${src:R}.pcm -MMD -MF ${.OBJDIR}/${src:R}.d",'
	echo >> ${_COMPDB} '"file": "${src}",'
	echo >> ${_COMPDB} '"output": "${src:R}.pcm"'
	echo >> ${_COMPDB} '},'
.endfor
.for src in ${SRCS:M*.cc}
	echo >> ${_COMPDB} '{'
	echo >> ${_COMPDB} '"directory": "${.CURDIR}",'
	echo >> ${_COMPDB} '"command": "${CXX} ${CPPFLAGS} ${CXXFLAGS} ${.CURDIR}/${src} -c -o ${src:R}.o -MMD -MF ${.OBJDIR}/${src:R}.d",'
	echo >> ${_COMPDB} '"file": "${src}",'
	echo >> ${_COMPDB} '"output": "${src:R}.o"'
	echo >> ${_COMPDB} '},'
.endfor

.for module in ${SRCS:M*.ccm}
${module:R}.module.o: ${module:R}.o
${module:R}.o: ${module:R}.pcm
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
