#!/usr/bin/env sh

if [ ! -d "build" ]; then
  mkdir build
fi

if [ ! -d "lib" ]; then
  mkdir lib
fi

if [ ! -d "bin" ]; then
  mkdir bin
fi

cp "NDI Advanced SDK for Linux"/include/* include/
cp "NDI Advanced SDK for Linux"/lib/aarch64-newtek-linux-gnu/* lib/

g++ -std=c++14 -pthread  -Wl,--allow-shlib-undefined -Wl,--as-needed -Iinclude/ -L lib -o build/ndi_monitor ndi_monitor.cpp mongoose.c mjson.c -lndi -ldl