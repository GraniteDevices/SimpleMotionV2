[![Linux Build Status](https://travis-ci.org/GraniteDevices/SimpleMotionV2.svg?branch=master)](https://travis-ci.org/GraniteDevices/SimpleMotionV2)
[![Windows Build Status](https://ci.appveyor.com/api/projects/status/github/GraniteDevices/SimpleMotionV2)](https://ci.appveyor.com/project/TeroK/simplemotionv2)

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
Following files are the core library. These must be included in the compiled projects to achieve a working solution.

- simplemotion.c/.h
- sm485.h
- sm_consts.c
- simplemotion_defs.h
- simplemotion_private.h
- busdevice.c/.h

Feature specific
----------------
Following files are not required in the core library, however you might need them depending on your application.

- bufferedmotion.c/.h - Library used for buffered motion stream applications
- devicedeployment.c/.h - Library used for installing firmware and loading settings into target devices. Can be used to configure devices automatically in-system.
- files in drivers/ folder - These are platofrm & hardware specific SM bus interface device drivers

Porting to new platform
-----------------------
Following files need modification if ported to another platform where SimpleMotion communication interface device driver does not yet exist

- If porting to a non-PC system, make sure that ENABLE_BUILT_IN_DRIVERS is not defined at compilation time (see qmake .pri file for clues) and use smOpenBusWithCallbacks instead of smOpenBus
- Write custom communication interface device driver and use smOpenBusWithCallbacks to open bus with custom driver callbacks
  - See existing drivers for example
  - Only four simple functions are needed to write custom port driver: open port, close port, write port and read port

Using SimpleMotion
==================
## In Qt Application
To include SM library in Qt project, simply place the library files under project folder and add the following line in your .pro file:

    include(SimpleMotionV2/SimpleMotionV2.pri)

## Creating shared / dynamic library
It is possible to compile shared and dynamic libraries (i.e. .dll or .a file) from the SM library source package. One of the easiest ways of compiling library is to use Qt Creator, where a ready-to-compile project file is provided. Just open SimpleMotionV2lib.pro in Qt Creator and compile it with the compiler of your choice. The resulting library files may be used in other applications.

Alternatively create a new library project in your favorite programming tool and compile the provided source codes into a libraray. You might need to study workings of the .pri file to succeed.

Windows users notice that a .dll library compiled with MinGW might not be compatible with MSVC. So if you're developing with MSVC, it's best to complile the dll using MSVC compiler. Qt Creator allows using Visual Studio's MSVC as compiler when configured properly.

## In Python, C# & others
Most of programming languages have wrapper solutions that can call C functions. To get started, search some examples from the Internet for your programmign language.

If you write a wrapper library or any form of working demo application to your non-C programming language, feel free to contribute it to us and get credited!

## In Visual Studio C/C++
If you wish to link library statically in other programming environments, i.e. in Visual Studio, it might be easiest to grap the sources and add them into the project so that they will be compiled along your project. This way library will be included in your application and it does not depend on external shared or dynamic libraries.

## In LabView
LabView allows including .dll file in the project and calling the C functions. To do that, first compile a .dll file as instructed in "Creating shared / dynamic library" section above.

Examples
========
For practical usage examples, refer to https://github.com/GraniteDevices/SimpleMotionV2Examples