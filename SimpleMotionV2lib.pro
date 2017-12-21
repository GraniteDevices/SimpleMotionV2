#-------------------------------------------------
#
# qmake project to compile static & dymaic libraries of SimpleMotion

# In windows, MinGW will output .a and .dll,
# MSVC2010 will ouput .lib and .dll.
# In linux, MinGW will output .so, .so.1, .so.1.0 and .so.1.0.0 - .lib, .a and .so are import libraries. They help link your code to the library and is needed when you build your file(.a files not all the time).
#
#-------------------------------------------------

include(SimpleMotionV2.pri)

QT       -= core gui

TARGET = simplemotionv2
TEMPLATE = lib

