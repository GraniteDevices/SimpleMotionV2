#User options

#if 0, then compile bare SM library without comm interface device drivers (user should then open bus with smOpenBusWithCallbacks and write custom port driver callbacks)
INCLUDE_BUILT_IN_DRIVERS = 1

#requires FTDI D2XX driver & library. benefit of this support is automatic detection of correct device and automatic low latency setting for FTDI USB serial converters
SUPPORT_FTDI_D2XX_DRIVER = 1

INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD


DEFINES += SIMPLEMOTIONV2_LIBRARY

SOURCES += $$PWD/sm_consts.c $$PWD/simplemotion.c $$PWD/busdevice.c \
    $$PWD/bufferedmotion.c $$PWD/devicedeployment.c

HEADERS += $$PWD/simplemotion_private.h\
    $$PWD/busdevice.h  $$PWD/simplemotion.h $$PWD/sm485.h $$PWD/simplemotion_defs.h \
    $$PWD/bufferedmotion.h $$PWD/devicedeployment.h


greaterThan(INCLUDE_BUILT_IN_DRIVERS, 0+)  {
    SOURCES += $$PWD/drivers/serial/pcserialport.c $$PWD/drivers/tcpip/tcpclient.c
    HEADERS += $$PWD/drivers/serial/pcserialport.h $$PWD/drivers/tcpip/tcpclient.h
    DEFINES += ENABLE_BUILT_IN_DRIVERS
    win32 {
        LIBS+=-lws2_32 #needed for tcp ip API
    }

    #If FTDI D2XX support is enabled
    greaterThan(SUPPORT_FTDI_D2XX_DRIVER, 0+)  {
        SOURCES += $$PWD/drivers/ftdi_d2xx/sm_d2xx.c
        HEADERS += $$PWD/drivers/ftdi_d2xx/sm_d2xx.c
        macx:LIBS              += $$PWD/drivers/ftdi_d2xx/third_party/osx/libftd2xx.a -framework CoreFoundation #mac will needs insetalling some FTDI helper tool & reboot to make port open to work. see d2xx downloads page from ftdi.
        win32:LIBS             += $$PWD/drivers/ftdi_d2xx/third_party/win_32bit/ftd2xx.lib
        linux:LIBS             += #tbd
        DEFINES += FTDI_D2XX_SUPPORT
    }
}


