# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

add_executable(netd)

target_compile_features(netd PUBLIC cxx_std_23)

target_link_libraries(netd PUBLIC
	netd.async
	netd.nvl
	netd.proto
	netd.util)

target_sources(netd PUBLIC
	netd.cc db.cc
)

target_sources(netd PUBLIC
	FILE_SET modules TYPE CXX_MODULES FILES
		netd.network.ccm
		netd.network-database.ccm
		ctl.ccm
		db.ccm
		iface.ccm
		log.ccm
		netlink.ccm
)

install(TARGETS netd DESTINATION sbin)

set(THIS_DIR $<TARGET_FILE_DIR:netd>)
set_property(GLOBAL APPEND_STRING PROPERTY _LIBTOOLING_EXTRA_ARGS "-fprebuilt-module-path=${THIS_DIR}/CMakeFiles/netd.dir ")
