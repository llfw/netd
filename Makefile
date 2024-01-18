#
# This source code is released into the public domain.
#

SUBDIRS		= netd netctl

default: all

.for _target in clean all depend install
${_target}:
.for dir in ${SUBDIRS}
	${MAKE} -C ${dir} ${_target}
.endfor
.endfor
