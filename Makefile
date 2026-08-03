.PHONY: build test clean tests

build:
	cmake -GNinja -Bbuild/ -DBUILD_EXAMPLES=1 -DUSE_ASAN=1 -DBUILD_TESTS=1 -DUSE_FORMAT=1 -DUSE_TIDY=1 -DKIT_FAULT_INJECTION=1
	ninja -C build/

test: build
	ninja -C build test

clean:
	rm -rf ./build/

tests: test
