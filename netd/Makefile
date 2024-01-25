#
# This source code is released into the public domain.
#

TARGET	= netd
BINDIR	= sbin
SRCS	= netd.cc	\
	  network.ccm network.cc	\
	  ctl.ccm ctl.cc		\
	  kq.ccm kq.cc			\
	  iface.ccm iface.cc		\
	  netlink.ccm netlink.cc	\
	  msgbus.ccm 			\
	  log.ccm			\
	  panic.ccm			\
	  task.ccm
LIBS	= -lnv
MODULES	= isam nvl uuid netd.proto netd.error

.include "../mk/prog.mk"
