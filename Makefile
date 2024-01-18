#
# This source code is released into the public domain.
#

SUBDIRS		= netd netctl

default: build

.for _target in clean build depend
${_target}:
.for dir in ${SUBDIRS}
	${MAKE} -C ${dir} ${_target}
.endfor
.endfor
