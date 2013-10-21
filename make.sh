#!/bin/sh

g++ -Wall -fPIC -g -c FirePick.cpp 
gcc -Wall FireFuse.c $(pkg-config fuse --cflags --libs) FirePick.o -o firefuse
