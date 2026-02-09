#!/bin/bash

git submodule update --init --recursive
COMMON_CMAKE_VARS="-DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14" \
    DISABLE_SANITIZER=1 DISABLE_VPTR_SANITIZER=1 GEN=ninja make release
cd evaluation
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 -GNinja
cmake --build .
cd ../..

export PATH="$PATH:$(pwd)/build/release"
