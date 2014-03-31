#!/bin/bash

BLOCK_SIZE="1M"
BLOCK_COUNT=100
IMAGE=$1

dd bs=$BLOCK_SIZE count=$BLOCK_COUNT if=/dev/zero of=$IMAGE
