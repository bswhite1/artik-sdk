#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Please specify the target architecture like armhf, arm64 "
	exit
fi
TARGET_ARCH=$1
BUILD_DIR=.
echo "Tager Architecture is $TARGET_ARCH"
echo "When cross-compiling, do not forget to set the environment variables CROSS_COMPILE, SYSROOT and CMAKE_PKG_CONFIG_PATH"
#rm -rf $BUILD_DIR/
#mkdir -p $BUILD_DIR/package/src/
#git archive -o update.tgz HEAD
#mv update.tgz $BUILD_DIR/package/src/
#cd $BUILD_DIR/package/src/
#tar -xvf update.tgz
#rm -f update.tgz
JOBS=$(nproc)
DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -j$JOBS -d -uc -us -a $TARGET_ARCH --target-arch $TARGET_ARCH
