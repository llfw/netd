#
# This source code is released into the public domain.
#

TOPDIR		:= ${.PARSEDIR:tA:H}

CC		= cc
WARNFLAGS	= -W -Wall -Wextra -Wmissing-variable-declarations \
		  -Wshorten-64-to-32 -Wsign-conversion
CPPFLAGS	= -I. -I${TOPDIR}/include 
CFLAGS		= -std=c17 -pedantic -O0 -g -fPIE \
		  -fstack-protector-strong ${WARNFLAGS}
LDFLAGS		= -pie

# clang's -Weverything is not supposed to be enabled by default, but only when
# linting to detect new warnings, so allow the user to enable it with a
# variable.
.if defined(WEVERYTHING)
WARNFLAGS	+= -Weverything
# ... but disable some warnings we definitely don't care about.
WARNFLAGS	+= -Wno-padded -Wno-format-nonliteral -Wno-cast-align	\
		   -Wno-unsafe-buffer-usage -Wno-covered-switch-default
.else
WARNFLAGS	+= -Werror
.endif

# flangs for clang-analyzer
ANALYSER_FLAGS	= \
	-enable-checker nullability.NullableDereferenced 	\
	-enable-checker nullability.NullablePassedToNonnull	\
	-enable-checker nullability.NullableReturnedFromNonnull	\
	-enable-checker optin.portability.UnixAPI		\
	-enable-checker valist.CopyToSelf			\
	-enable-checker valist.Uninitialized			\
	-enable-checker valist.Unterminated

PREFIX		?= /usr/local

.export CC CPPFLAGS CFLAGS LDFLAGS