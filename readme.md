# Raspberry Pi HDMI Input

This is a proof of concept code for capturing a decoded HDMI signal through the Raspberry Pi's GPIO pins. This program captures a single image from the HDMI input, displays it and optionally saves it as a PNG.

## The Idea

The idea was to build a monitor with an 800x600 4-bit E-Ink display. The Raspberry Pi was chosen over a microcontroller due to the amount of RAM required to store one full frame.
The only ready-made way of capturing a HDMI signal with the Pi at the time was a HDMI to CSI conversion board. This board was rather expensive, and it was unsure if the required resolution of 800x600 pixels would be supported.
Since the E-Ink only requires 16 greyscales at a framerate of just a few fps at best, I came up with a way to use the GPIO instead.

## Technology

The HDMI signal is decoded into 24-bit parallel RGB by a decoder board based around the TFP401 IC. Because the SPI peripheral on the Pi doesn't support slave mode, the PCM peripheral is used to shift in the pixel data instead.
Some logic is used to gate the pixel clock, such that following a start signal, only the active pixel data is shifted in from the start of the next frame.
Since this is supposed to be used with mostly static content, color capture is achieved by capturing 6 consecutive frames, each containing one of the two MSBs of the red, green, and blue channel.
The software assembles those frames into a single, 6-bit color image.

## Known Issues

The PCM peripheral needs a few clock cycles to finish the setup before any data is being read. This leads to a shift in the captured pixel data. This can be resolved either in hardware or software.

## Building

This code was tested on the Raspberry Pi 3B and the Raspberry Pi 4B. Since those two models use different SOCs and this code relies on low-level peripheral interactions, 
the matching physical base address needs to be uncommented in periLib.h before compiling.

The HEX data in the edid.dat file has to be uploaded to the EEPROM on the TFP401 board to request the correct video format.
