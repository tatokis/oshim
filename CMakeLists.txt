cmake_minimum_required(VERSION 3.5.1)
project(oshim)

add_library(oshim SHARED shim.c shim_xlib.c inlines.h structs.h)
# Warnings
target_compile_options(oshim PRIVATE -Wall -Wextra -Wno-unused-parameter -Wcast-align)
# Needed for backtrace()
target_compile_options(oshim PRIVATE -rdynamic)
target_link_libraries(oshim "dl" "xcb" "xcb-keysyms" "xcb-xinput" "xcb-xkb")
set_property(TARGET oshim PROPERTY C_STANDARD 11)
