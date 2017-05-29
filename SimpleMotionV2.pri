
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD


DEFINES += SIMPLEMOTIONV2_LIBRARY

SOURCES += $$PWD/sm_consts.c $$PWD/simplemotion.c $$PWD/busdevice.c $$PWD/pcserialport.c \
    $$PWD/bufferedmotion.c $$PWD/tcpclient.c $$PWD/devicedeployment.c

HEADERS += $$PWD/simplemotion_private.h\
    $$PWD/pcserialport.h $$PWD/busdevice.h  $$PWD/simplemotion.h $$PWD/sm485.h $$PWD/simplemotion_defs.h \
    $$PWD/bufferedmotion.h $$PWD/tcpclient.h $$PWD/devicedeployment.h

