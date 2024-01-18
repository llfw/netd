#
# This source code is released into the public domain.
#

TOPDIR		:= ${.PARSEDIR:tA:H}

CC		= cc
WARNFLAGS	= -W -Wall -Wextra -Werror
CPPFLAGS	= -I. -I${TOPDIR}/include 
CFLAGS		= -std=c17 -pedantic -O0 -g -fPIE \
		  -fstack-protector-strong ${WARNFLAGS}
LDFLAGS		= -pie

PREFIX		?= /usr/local

.export CC CPPFLAGS CFLAGS LDFLAGS
