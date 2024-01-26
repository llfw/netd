#
# This source code is released into the public domain.
#

TARGET	= netd
BINDIR	= sbin
SRCS	= netd.cc			\
	  network.ccm network.cc	\
	  ctl.ccm ctl.cc		\
	  kq.ccm kq.cc			\
	  iface.ccm iface.cc		\
	  netlink.ccm netlink.cc	\
	  log.ccm			\
	  panic.ccm			\
	  task.ccm
LIBS	= -lnv
MODULES	= netd.isam nvl uuid netd.proto netd.error netd.rate netd.event

.include "../mk/prog.mk"
