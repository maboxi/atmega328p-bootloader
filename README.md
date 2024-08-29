# Bootloader + Python program for the Atmega328P chip

This project contains a custom bootloader program for the Atmega328P (known for the Arduino family boards) which fulfills basic bootloader functionalities:
- Uploading a program (given by a Intel Hex format file) to the microcontroller
- Verifiying an upload
- Reading the fuse bytes (extended, high, low) and interpreting the values

The python program can act as either a command-line tool for the usage of the bootloader or as a way to have a live interaction with the bootloader as a user.


## Atmega328P Bootloader

The bootloader can be addressed using the UART interface of the microcontroller. The instructions are basic ASCII characters, the data e.g. for uploading a program is transfered byte-wise.

The bootloader currently supports the following instructions:
- 'i': Query information about the bootloader. This returns the bootloader version as well as the start address of the bootloader section in the flash of the microcontroller to allow section checks in the uploading program.
- 'q': Quit the bootloader and start the application located at 0x0
- 'u': Upload a hex file to the application section of the flash memory
- 'v': Verify sections of the flash memory. The bootloader only reads out the memory, verification has to happen in the tool that addresses the bootloader
- 'f': Reads the fuse bytes (extended, high, low) and the locks byte from the microcontroller. The tool then decodes these bytes and displays the resulting microcontroller configuration


## Python Bootloader-Tool

Python tool usage (developed using Python 3.12.0):

    usage: uploader.py [-h] --port PORT [--baudrate BAUDRATE] [-f FILE]
                    [--no-upload] [--no-verify] [-r] [-i] [--no-quit] [-v]

    Upload firmware to Atmega328p based devices that run the corresponding
    bootloader

    options:
        -h, --help            show this help message and exit
        --port PORT, -p PORT  serial port
        --baudrate BAUDRATE   baudrate of serial connection
        -f FILE, --file FILE  firmware hex file
        --no-upload           skip upload
        --no-verify           skip upload verification
        -r, --fuses           read fuses
        -i, --info
        --no-quit             don't quit bootloader after tasks are finished
        -v, --verbose

## Useful AVRDUDE Commands

- Read fuses: -U lfuse|hfuse|efuse:r:-:b -> Displays binary representation of fuse values
- Write fuses: -U lfuse|hfuse|efuse:w:<fuseval>:m -> Writes the given fuseval into the selected fuse byte
    - ATTENTION: don't forget the number system prefix before immediate values (0x... / 0b...)! Else you will probably brick the microcontroller by disabling the external reset!!! (this definitely didn't happen to me....)


## AVR Specifications and other interesting links

- Atmega328P Datasheet: https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf
- AVR Libc Documentation: https://www.nongnu.org/avr-libc/user-manual/index.html
- AVR Instruction Set: https://ww1.microchip.com/downloads/en/devicedoc/atmel-0856-avr-instruction-set-manual.pdf
- Inline Assembler: https://www.nongnu.org/avr-libc/user-manual/inline_asm.html