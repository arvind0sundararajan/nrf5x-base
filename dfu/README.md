DFU Bootloader
==========

DFU Bootloader to support over-the-air firmware updates.
Currently supports devices using SDK 10, softdevice s110, with 32 KB RAM and
256 KB Flash.

To include bootloader with app, simply add this line to your app Makefile:
    
    USE_BOOTLOADER = 1

