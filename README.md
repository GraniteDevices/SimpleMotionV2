SimpleMotionV2
==============

This is a SimpleMotion V2 library, which is an API to control motor controller from any programmable platform, such as PC, PLC or MCU.

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

Platform specific
-----------------
Following files probably need modification if ported to another platform

- pcserialport.c/.h - Contains a driver for communication device interface. This driver controls serial/COM port on Unix & Windows. Also busdevice.c/.h need to be modified accordingly if this driver is removed or modified.

A typical platform port would involve writing a communication driver that implements same functions as pcserialport.c and adding their relevant calls to busdevice.c/.h.

Feature specific
----------------

- bufferedmotion.c/.h - Library used for buffered motion stream applications
- devicedeployment.c/.h - Library used for installing firmware and loading settings into target devices. Can be used to configure devices automatically in-system.

For practical usage examples, refer to https://github.com/GraniteDevices/SimpleMotionV2Examples