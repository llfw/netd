#
# This source code is released into the public domain.
#

TARGET	= netd
BINDIR	= sbin
SRCS	= netd.cc kq.cc netlink.cc iface.cc log.cc ctl.cc msgbus.cc	\
	  network.ccm network.cc
LIBS	= -lnv
MODULES	= isam nvl uuid

.include "../mk/prog.mk"
