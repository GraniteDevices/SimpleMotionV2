
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD


DEFINES += SIMPLEMOTIONV2_LIBRARY

SOURCES += $$PWD/sm_consts.c $$PWD/simplemotion.c $$PWD/busdevice.c $$PWD/rs232.c \
    $$PWD/bufferedmotion.c

HEADERS += $$PWD/simplemotion_private.h\
    $$PWD/rs232.h $$PWD/busdevice.h  $$PWD/simplemotion.h $$PWD/sm485.h $$PWD/simplemotion_defs.h \
    ../SimpleMotionV2Library/bufferedmotion.h

