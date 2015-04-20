#!/bin/bash

set -e

PROCESSOR=K22F51212

sep() {
    echo "------------------"
}


if [ -z "$ARMGCC_DIR" ]; then
    echo "Need to set ARMGCC_DIR to the root of an ARM eabi-newlib compiler chain."
    echo "If you are using the Freescale Kinetis environment, one is available at "
    echo "/opt/Freescale/KDS_2.0.0/toolchain"
    exit -1
fi

CMAKE_PATH=$(which cmake)
if [ -z $CMAKE_PATH ]; then
    echo "Please install CMake. The Kinetis SDK requires CMake for Makefile generation."
    exit -1
fi

sep
echo "Bootstrapping KSDK"
sep

pushd .
cd ksdk/lib/ksdk_platform_lib/armgcc/$PROCESSOR
sh build_all.sh
popd

pushd .
cd ksdk/lib/ksdk_freertos_lib/armgcc/$PROCESSOR
sh build_all.sh
popd
