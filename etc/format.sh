#!/bin/bash

IMAGE=$1
FORMAT=$2
BDEV=`sudo losetup -f`

sudo losetup $BDEV $IMAGE
sudo $FORMAT $BDEV
sudo losetup -d $BDEV
