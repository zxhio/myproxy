cmake_minimum_required(VERSION 3.10)
project(myproxy VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

# Get version from environment variable or default
if(DEFINED ENV{VERSION})
    set(PROJECT_VERSION $ENV{VERSION})
else()
    set(PROJECT_VERSION "0.0.1")
endif()

# Get build date
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")
if(NOT BUILD_DATE)
    set(BUILD_DATE "unknown")
endif()

# Get git commit hash (short)
execute_process(
    COMMAND git log -1 --format=%h
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-strict-aliasing")

# Static linking (Linux only)
if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
endif()

# libev as static library
add_library(ev STATIC
    third-party/libev-4.33/ev.c
    third-party/libev-4.33/event.c
)
target_include_directories(ev PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third-party/libev-4.33)

# Platform-specific libev macros
if(APPLE)
    target_compile_definitions(ev PUBLIC
        EV_STANDALONE=1
        EV_USE_KQUEUE=1
        EV_USE_REALTIME=1
        EV_USE_MONOTONIC=1
        EV_USE_SELECT=1
        EV_USE_POLL=1
        EV_USE_NANOSLEEP=1
    )
else() # Linux
    target_compile_definitions(ev PUBLIC
        EV_STANDALONE=1
        EV_USE_EPOLL=1
        EV_USE_INOTIFY=1
        EV_USE_EVENTFD=1
        EV_USE_CLOCK_SYSCALL=0
        EV_USE_REALTIME=1
        EV_USE_MONOTONIC=1
        EV_USE_SELECT=1
        EV_USE_POLL=1
        EV_USE_NANOSLEEP=1
    )
endif()

# Suppress warnings from libev
target_compile_options(ev PRIVATE -w)

# myproxy executable
add_executable(myproxy myproxy.c)
target_link_libraries(myproxy PRIVATE ev)
target_compile_definitions(myproxy PRIVATE
    MYPROXY_VERSION="${PROJECT_VERSION}"
    MYPROXY_BUILD_DATE="${BUILD_DATE}"
    MYPROXY_COMMIT_HASH="${GIT_COMMIT_HASH}"
)

# echo_server for testing
add_executable(echo_server tests/echo_server.c)
target_link_libraries(echo_server PRIVATE ev)

# Strip debug symbols from binaries
add_custom_command(TARGET myproxy POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:myproxy>)

install(TARGETS myproxy DESTINATION bin)
