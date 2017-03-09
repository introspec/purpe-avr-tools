# purpe-avr-tools
A SPI based AVR programmer for Raspberry Pi boards

## Connections

Connect the Raspberry Pi MOSI, MISO and SCLK to the AVR MOSI, MISO and SCLK 
pins.

Provide power to the AVR via the Raspberry Pi 3.3v and GND pins or using
an external power supply.

In case of an external power supply, connect the Pi GND to the power supply
GND (I.e., the AVR GND).


Usage:
	avr-spi-prog 			(will print fuse, lock and signature)
	avr-spi-prog -e -p <file> 	(will program file at offset 0)

Use the -b <value> flag to change the programming offset.


