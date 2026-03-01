cmake_minimum_required(VERSION 3.10)
project(myproxy VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

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

# echo_server for testing
add_executable(echo_server tests/echo_server.c)
target_link_libraries(echo_server PRIVATE ev)

# Strip debug symbols from binaries
add_custom_command(TARGET myproxy POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:myproxy>)

install(TARGETS myproxy DESTINATION bin)
