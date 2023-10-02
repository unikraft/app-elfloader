#!/bin/bash

test ! -d "workdir" && echo "Cloning repositories ..." || true
test ! -d "workdir/unikraft" && git clone https://github.com/unikraft/unikraft workdir/unikraft || true
test ! -d "workdir/libs/lwip" && git clone https://github.com/unikraft/lib-lwip workdir/libs/lwip || true
test ! -d "workdir/libs/libelf" && git clone https://github.com/unikraft/lib-libelf workdir/libs/libelf || true
