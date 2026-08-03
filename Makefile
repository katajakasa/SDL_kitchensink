.PHONY: release build-asan build-tsan test-asan test-tsan clean

release:
	cmake -GNinja -Bbuild/ \
		-DBUILD_EXAMPLES=1 \
		-DUSE_ASAN=0 \
		-DUSE_TSAN=0 \
		-DBUILD_TESTS=1 \
		-DUSE_FORMAT=0 \
		-DUSE_TIDY=0 \
		-DKIT_FAULT_INJECTION=0 \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=./build/release
	ninja -C build/
	ninja -C build/ install

build-asan:
	cmake -GNinja -Bbuild/ \
		-DBUILD_EXAMPLES=1 \
		-DUSE_ASAN=1 \
		-DUSE_TSAN=0 \
		-DBUILD_TESTS=1 \
		-DUSE_FORMAT=1 \
		-DUSE_TIDY=1 \
		-DKIT_FAULT_INJECTION=1 \
		-DCMAKE_BUILD_TYPE=Debug
	ninja -C build/

build-tsan:
	cmake -GNinja -Bbuild/ \
		-DBUILD_EXAMPLES=1 \
		-DUSE_ASAN=0 \
		-DUSE_TSAN=1 \
		-DBUILD_TESTS=1 \
		-DUSE_FORMAT=1 \
		-DUSE_TIDY=1 \
		-DKIT_FAULT_INJECTION=1 \
		-DCMAKE_BUILD_TYPE=Debug
	ninja -C build/

test-asan: build-asan
	ninja -C build test

test-tsan: build-tsan
	ninja -C build test

clean:
	rm -rf ./build/
