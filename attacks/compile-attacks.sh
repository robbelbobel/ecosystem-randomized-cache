#!/bin/bash
cp ./* ../newlib-bsp
cd ../newlib-bsp
EXTRA_OBJECTS="dynamic_array.o" make evsetcreation.bin
make cache-test.bin 