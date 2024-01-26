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

cmake_minimum_required(VERSION 3.28)

project(netd)

set(CMAKE_CXX_STANDARD 23)

set(NETD_WARNFLAGS "\
-W;-Wall;-Wextra;-Werror;-Wshorten-64-to-32;-Wsign-conversion;\
-Wimplicit-fallthrough\
")

add_compile_options(
"$<$<COMPILE_LANGUAGE:CXX>:-pedantic;-fPIE;-fstack-protector-strong>;\
${NETD_WARNFLAGS}"
)
add_link_options("-pie")

option(SANITIZE "Enable clang sanitizers (ASAN, UBSAN)" OFF)

if(SANITIZE)
	add_compile_options("-fsanitize=address;-fsanitize=undefined")
	add_link_options("-fsanitize=address;-fsanitize=undefined")
endif()

# This doesn't work yet since clang-analyser doesn't seem to understand
# modules.
set(lint_dir ${CMAKE_BINARY_DIR}/lint)
set(lint_scan_build_path scan-build)
set(lint_reports_path ${CMAKE_BINARY_DIR}/lint_reports)

add_custom_target(lint_prepare
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${lint_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${lint_dir}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(lint
	COMMAND ${lint_scan_build_path} ${CMAKE_COMMAND} ${CMAKE_SOURCE_DIR}
			-GNinja -DCMAKE_BUILD_TYPE=Debug
	COMMAND ${lint_scan_build_path} --status-bugs -v -v
		-o ${lint_reports_path}
		${CMAKE_COMMAND} --build .
	WORKING_DIRECTORY ${lint_dir}
)

# Run the *_prepare target always before the analysis
add_dependencies(lint lint_prepare)

include_directories(include)

add_subdirectory(src)
