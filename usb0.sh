#! /bin/bash
stty 0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0 -F /dev/ttyUSB0; stty -g -F /dev/ttyUSB0
stty 115200 -F /dev/ttyUSB0; stty -g -F /dev/ttyUSB0
stty cs8 -F /dev/ttyUSB0; stty -g -F /dev/ttyUSB0
stty -cstopb -F /dev/ttyUSB0; stty -g -F /dev/ttyUSB0
stty -parenb -F /dev/ttyUSB0; stty -g -F /dev/ttyUSB0
