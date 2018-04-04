cd $TRAVIS_BUILD_DIR
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_EXAMPLES=On -DCMAKE_C_COMPILER=/usr/bin/gcc-7 -DUSE_TESTS=Off . 
make
make test
make clean
build-wrapper-linux-x86-64 --out-dir bw-output make
sonar-scanner
