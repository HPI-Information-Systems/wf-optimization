
LLVM_PATH ?= /usr/

.PHONY: duckdb clean main

all: duckdb microbench

clean:
	rm -rf build

duckdb:
	cd ../.. && DISABLE_SANITIZER=1 CXX_STANDARD=17 CMAKE_LLVM_PATH=${LLVM_PATH} DISABLE_VPTR_SANITIZER=1 make release

main:
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .
	build/example

microbench: build
	build/microbench

build: duckdb mb

mb:
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${LLVM_PATH}/bin/clang -DCMAKE_CXX_COMPILER=${LLVM_PATH}/bin/clang++ && cmake --build .
