#!/bin/bash

clear 

# rm -rf build bin lib
rm app.log


cmake -S . -B build
cmake --build build -j$(nproc)

echo ""
echo "执行结果 :"
echo ""
echo ""
./bin/loggerTest