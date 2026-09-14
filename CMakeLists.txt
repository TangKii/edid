cmake_minimum_required(VERSION 3.10)
project(edid C)

set(CMAKE_C_STANDARD 99)

add_executable(edid
    main.c
    edid_parser.c
)

target_include_directories(edid PRIVATE ${CMAKE_SOURCE_DIR})
