SimpleMotionV2
==============

This is a SimpleMotion V2 library, which is an API to control motor controller from any programmable platform, such as PC (Linux, Win, Mac), Rasperry Pi, MCU or PLC system.

For main documentation, see:
http://granitedevices.com/wiki/SimpleMotion_V2


Files & usage
=============

There are files that are needed for basic SimpleMotion usage, and files that are optional and needed only for certain features. Following may be useful info when porting library to different platforms (such as embedded MCU) to avoid unnecessary work on some files.

Compulsory
----------

- simplemotion.c/.h
- sm485.h
- sm_consts.c
- simplemotion_defs.h
- simplemotion_private.h
- busdevice.c/.h

Porting to new platform
-----------------------
Following files need modification if ported to another platform where SimpleMotion communication interface device driver does not yet exist

- If porting to a non-PC system, make sure that ENABLE_BUILT_IN_DRIVERS is not defined at compilation time (see qmake .pri file for clues) and use smOpenBusWithCallbacks instead of smOpenBus
- Write custom communication interface device driver and use smOpenBusWithCallbacks to open bus with custom driver callbacks
  - Ssee existing drivers for example
  - Only four simple functions are needed to write custom port driver: open port, close port, write port and read port

Feature specific
----------------

- bufferedmotion.c/.h - Library used for buffered motion stream applications
- devicedeployment.c/.h - Library used for installing firmware and loading settings into target devices. Can be used to configure devices automatically in-system.

For practical usage examples, refer to https://github.com/GraniteDevices/SimpleMotionV2Examples