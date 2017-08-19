#User options
SUPPORT_FTDI_D2XX_DRIVER = 1 #requires FTDI D2XX driver & library. benefit of this support is automatic detection of correct device and automatic low latency setting for FTDI USB serial converters

INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD


DEFINES += SIMPLEMOTIONV2_LIBRARY

SOURCES += $$PWD/sm_consts.c $$PWD/simplemotion.c $$PWD/busdevice.c $$PWD/pcserialport.c \
    $$PWD/bufferedmotion.c $$PWD/tcpclient.c $$PWD/devicedeployment.c

HEADERS += $$PWD/simplemotion_private.h\
    $$PWD/pcserialport.h $$PWD/busdevice.h  $$PWD/simplemotion.h $$PWD/sm485.h $$PWD/simplemotion_defs.h \
    $$PWD/bufferedmotion.h $$PWD/tcpclient.h $$PWD/devicedeployment.h


#If FTDI D2XX support is enabled
greaterThan(SUPPORT_FTDI_D2XX_DRIVER, 0+)  {
    SOURCES += $$PWD/drivers/ftdi_d2xx/sm_d2xx.c
    macx:LIBS              +=
#    win32:LIBS             += $$PWD/drivers/ftdi_d2xx/static_w32_i386_ftd2xx.lib
    win32:LIBS             += $$PWD/drivers/ftdi_d2xx/ftd2xx.lib
    linux:LIBS             +=
    DEFINES += FTDI_D2XX_SUPPORT
}
