

.PHONY: duckdb clean main

all: duckdb microbench

clean:
	rm -rf build

duckdb:
	cd ../.. && DISABLE_SANITIZER=1 make

main:
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .
	build/example

microbench: build
	build/microbench

build: duckdb mb

mb:
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .
