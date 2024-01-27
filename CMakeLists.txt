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

add_compile_options(${NETD_WARNFLAGS})
add_compile_options("-pedantic;-fstack-protector-strong")

option(SANITIZE "Enable clang sanitizers (ASAN, UBSAN)" OFF)
option(TIDY "Enable clang-tidy" OFF)
option(ANALYZE "Enable clang-tidy static analyser" OFF)
option(PIE "Produce position-independent executables" ON)

# PIE is not compatible with SANITIZE
if(SANITIZE AND PIE)
	message(FATAL_ERROR "SANITIZE and PIE cannot be enabled at the same time")
endif()

if(PIE)
	add_link_options("-pie")
	add_compile_options("-fPIE")
endif()

if(SANITIZE)
	add_compile_options("-fsanitize=address;-fsanitize=undefined")
	add_link_options("-fsanitize=address;-fsanitize=undefined")
endif()

set(lint_dir ${CMAKE_BINARY_DIR}/lint)

add_custom_target(lint_prepare
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${lint_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${lint_dir}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(lint
	COMMAND ${CMAKE_COMMAND} ${CMAKE_SOURCE_DIR}
		-DTIDY:ON -GNinja -DCMAKE_BUILD_TYPE=Debug
	COMMAND ${CMAKE_COMMAND} --build .
	WORKING_DIRECTORY ${lint_dir}
)

add_dependencies(lint lint_prepare)

# clang-format; adds a 'ninja format' target.
file(GLOB_RECURSE ALL_SOURCES
	${CMAKE_SOURCE_DIR}/*.cc
	${CMAKE_SOURCE_DIR}/*.ccm
	${CMAKE_SOURCE_DIR}/*.hh)
add_custom_target(format COMMAND clang-format -i ${ALL_SOURCES})

# clang-tidy
if(ANALYZE)
	set(_TIDY_EXTRA_ARGS "--checks=clang-static-analyzer-*;")
else()
	set(_TIDY_EXTRA_ARGS "")
endif()

if(TIDY)
	set(CMAKE_CXX_CLANG_TIDY
	  clang-tidy;
	  -format-style='file';
	  ${_TIDY_EXTRA_ARGS}
	  --extra-arg-before=--config=${CMAKE_CURRENT_BINARY_DIR}/clang-tidy.opts
	  -header-filter=^${CMAKE_SOURCE_DIR}.*;
	)
endif()

include_directories(include)
add_subdirectory(src)

get_property(_LIBTOOLING_EXTRA_ARGS GLOBAL PROPERTY _LIBTOOLING_EXTRA_ARGS)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clang-tidy.opts CONTENT ${_LIBTOOLING_EXTRA_ARGS})
