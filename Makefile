#
# This source code is released into the public domain.
#

SUBDIRS		= modules netd netctl

default: all

.for _target in clean all install
${_target}:
.for dir in ${SUBDIRS}
	${MAKE} -C ${.CURDIR}/${dir} ${_target}
.endfor
.endfor

depend:
	${MAKE} -C ${.CURDIR}/p1689make
	rm -f ${_COMPDB}
	echo >>${_COMPDB} '['
.for dir in ${SUBDIRS}
	${MAKE} -C ${.CURDIR}/${dir} depend
.endfor
	truncate -s -2 ${_COMPDB}
	echo >>${_COMPDB} ']'
	clang-scan-deps --format=p1689 --compilation-database=${_COMPDB} >${_P1689FILE}
	${P1689MAKE} ${_P1689FILE} > ${_MDEPSFILE}

lint:
	scan-build ${ANALYSER_FLAGS} --status-bugs ${MAKE} clean all

.include "mk/vars.mk"
