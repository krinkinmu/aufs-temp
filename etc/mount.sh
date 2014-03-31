#!/bin/bash

IMAGE=$1
DIR=$2
BDEV=loop
FSNAME=aufs
USR=`whoami`

sudo mount -o $BDEV -t $FSNAME $IMAGE $DIR
sudo chown $USR:$USR $DIR
sudo chmod 755 $DIR
