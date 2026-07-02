cmake_minimum_required(VERSION 3.10)
project(lc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(lc
    src/main.c
    src/strbuf.c
    src/pathutil.c
    src/config.c
    src/template.c
    src/init.c
)

if(WIN32)
    target_link_libraries(lc PRIVATE)
endif()

if(UNIX AND NOT APPLE)
    # Nothing extra required; readlink(/proc/self/exe) is glibc/POSIX.
endif()

install(TARGETS lc DESTINATION bin)
