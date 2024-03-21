# esp32-eth-serial
ESP32 ethernet to serial port bridge

## Motivation

There are no readily available Ethernet to UART bridge having high bit rate with hardware flow control and low price. For example the EBYTE NT1 Ethernet bridge has maximum baud rate of 230400.
The ESP32 on the other hand provides an excellent platform for Ethernet to UART bridge implementation. We use WT32-ETH01 'wireless tag' module. Yet the code is may be easily ported to any other
module having Ethernet interface.

## Build environment

See https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html

## Building
```
git clone git@github.com:olegv142/esp32-eth-serial.git
cd esp32-eth-serial/src
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

## Configuring

The bridge has connection indicator output, serial data RX/TX lines and flow control lines RTS/CTS, the last one is optional. All pin locations can be configured by running *idf.py menuconfig*. Besides one can configure UART baud rate, buffer size and whether to use CTS flow control line. The supported serial baud rates are in the range from 9600 to 1843200 with 921600 being the default.

## Flashing

Unless you have dev kit with USB programmer included you will need some minimal wiring made to the ESP32 module to be able to flash it. The following figure shows an example of such setup with programming connections shown in blue. The connections providing serial interface to the your system are shown in black.

![ESP32 module wiring](https://github.com/olegv142/esp32-eth-serial/blob/master/doc/wiring.png)

You can use virtually any USB-serial bridge capable of working at 115200 baud. The IO0 pin shoud be connected to the ground while powering up the module in order to turn it onto serial programming mode. After that you can issue *idf.py flash* command in the project directory and wait for the flashing completion. Then turn off power, disconnect programming circuit and enjoy using your brand new Ethernet to serial bridge.

## Connections

You can use hardware flow control CTS/RTS lines or ignore them depending on your system design details. Basically not using RTS line is safe if packets you are sending to the module's RXD line are not exceeding 128 bytes. The CTS line usage is completely up to your implementation of the serial data receiver. If you are not going to use CTS line you should either connect it to the ground or disable at firmware build stage by means of *idf.py menuconfig*. The EN line plays the role of the reset to the module. Low level on this line turns the module onto the reset state with low power consumption. In case you are not going to use this line it should be pulled up. The pull up resistors on the TXD and RTS lines are needed to prevent them from floating during module boot.

## Troubleshooting

The ESP32 module is using the same serial channel used for programming to print error and debug messages. So if anything goes wrong you can attach the programming circuit without grounding the IO0 pin and monitor debug messages by calling *idf.py -p <serial-port> monitor*.

## Power consumption

130mA in idle state, up to 150mA while transferring data at maximum rate.

## Framework version

The code was built with esp-idf version 5.2.1.

