# Bootloader + Python program for the Atmega328P chip

This project contains a custom bootloader program for the Atmega328P (known for the Arduino family boards) which fulfills basic bootloader functionalities:
- Uploading a program (given by a Intel Hex format file) to the microcontroller
- Verifiying an upload
- Reading the fuse bytes (extended, high, low) and interpreting the values

The python program can act as either a command-line tool for the usage of the bootloader or as a way to have a live interaction with the bootloader as a user.