cmake_minimum_required(VERSION 3.10)
project(myproxy VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

# Compiler flags
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4 /O2")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")
    # libev macros trigger strict-aliasing warnings, disable them
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-strict-aliasing")
endif()

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
if(WIN32)
    target_compile_definitions(ev PUBLIC
        EV_STANDALONE=1
        EV_USE_SELECT=1
        EV_USE_POLL=1
        EV_USE_WIN32=1
    )
    # Link winsock on Windows
    target_link_libraries(ev PUBLIC ws2_32)
elseif(APPLE)
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
if(NOT MSVC)
    target_compile_options(ev PRIVATE -w)
endif()

# myproxy executable
add_executable(myproxy myproxy.c)
target_link_libraries(myproxy PRIVATE ev)

install(TARGETS myproxy DESTINATION bin)
