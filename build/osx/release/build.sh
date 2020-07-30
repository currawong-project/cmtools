#!/bin/sh

curdir=`pwd`

cd ../../..
autoreconf --force --install

cd ${curdir}

../../../configure --prefix=${curdir} \
CFLAGS="-Wall" \
CXXFLAGS="-Wall" \
CPPFLAGS= \
LDFLAGS= \
LIBS=


#make
#make install