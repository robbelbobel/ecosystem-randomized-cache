#!/bin/bash

set -ex

# Use Proteus Randomized Cache Fork
git clone --recurse-submodules --branch main https://github.com/robbelbobel/proteus-randomized-cache.git core 

make -C simulation clean
make -C simulation CORE=riscv.CoreDynamicExtMem
make -C functional-tests CORE=riscv.CoreDynamicExtMem BUILD_CORE=0 RISCV_PREFIX=riscv64-unknown-elf
