#!/bin/bash

if [ -z "$1" ]; then
    echo -e "Call $0 <esp32 IP address> to run this test"
    exit 1
fi

echo Testing echo server throughput. Type Ctrl-C to stop ...
dd if=/dev/urandom | nc -N $1 3333 > /dev/null
