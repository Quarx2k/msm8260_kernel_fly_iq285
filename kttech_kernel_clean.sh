#!/bin/sh
# 
# Linux kernel make script to build kernel.
# 
# Copyright (C) 2012-2011 KT Tech, Inc
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# 

#########################################################################
# Plase insert to below build informations.                             # 
#########################################################################
# CROSS_COMPILE_DIR  : Cross Compiler binary directory. 
# CROSS_COMPILE_NAME : Cross Compiler name. (eg. arm-eabi-)
# BUILD_OUT_HOME     : Kernel Built out HOME Directory.
########################################################################
 
# Cross-compiler root dir & name. (Required)
# You need to get toolchain and install and Please refer to visit below.
# 1) http://www.codesourcery.com/sgpp/portal/release1033
# 2) https://www.codeaurora.org/gitweb/quic/la/?p=platform/prebuilt.git;a=summary

CROSS_COMPILE_DIR=/home/jhoonkim/arm-eabi-4.4.3/bin
CROSS_COMPILE_NAME=arm-eabi-

# Kernel Image built out dir. (Required. Default : Home Directory)
BUILD_OUT_HOME=/home/jhoonkim

# Kernel Boot Command Line. (You can able to add additional boot command line.)
KERNEL_CMDLINE='console=ttyHSL0,115200,n8 androidboot.hardware=qcom no_console_suspend=1'

# Additional Make Options
MAKE_OPTIONS=-j12

#########################################################################
# Don't touch below lines.                                              #
#########################################################################

# KT Tech Model name.
MODEL_NAME=KM-S200
KERNEL_DEFCONFIG=msm8660_o3_defconfig
KERNEL_BASE_ADDR=0x40200000
KERNEL_PAGE_SIZE=2048
KERNEL_PLATFORM_VER=pp
KERNEL_BUILD_FINAL=true

BUILD_OUT_NAME=$BUILD_OUT_HOME/$MODEL_NAME-kernel
BOOT_IMAGE_NAME=boot.img

# Etc informations.
KERNEL_FLASH_CMD="fastboot flash boot boot.img"

# Ramdisk Image, Image build tools dir & name.
IMAGE_TOOLS_DIR=./tools/kttech
RAMDISK_IMAGE_NAME=$IMAGE_TOOLS_DIR/ramdisk.img
IMAGE_TOOL_NAME=$IMAGE_TOOLS_DIR/mkbootimg 

echo "###############################################"
echo " KT Tech kernel build script for $MODEL_NAME"
echo "###############################################"
echo " Clean kernel build objects..."

export PATH=$PATH:CROSS_COMPILE_DIR
export KTTECH_BOARD=$KERNEL_PLATFORM_VER
export KERNEL_BUILD_FINAL=$KERNEL_BUILD_FINAL

make mrproper
make O=$BUILD_OUT_NAME/KERNEL_OBJ clean
rm -rf $BUILD_OUT_NAME

echo " Completed."

echo "###############################################"
echo " Clean completed."
echo "###############################################"
