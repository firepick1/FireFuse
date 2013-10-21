#!/bin/sh

gcc -Wall FireFuse.c $(pkg-config fuse --cflags --libs) -o firefuse
