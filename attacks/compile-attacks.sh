#!/bin/bash
cp ./* ../newlib-bsp
cd ../newlib-bsp
EXTRA_OBJECTS="dynamic_array.o" make evsetcreation.bin
EXTRA_OBJECTS="dynamic_array.o" make evsetcreation2.bin
EXTRA_OBJECTS="dynamic_array.o" make evsetcreation3.bin
make evset2.bin
make cache-test.bin 
make cache-pressure-test.bin
