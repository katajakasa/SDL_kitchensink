sudo add-apt-repository ppa:zoogie/sdl2-snapshots -y
sudo add-apt-repository ppa:george-edison55/cmake-3.x -y
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo add-apt-repository ppa:jonathonf/ffmpeg-3 -y
sudo apt-get update
sudo apt-get -y install \
    gcc-7 \
    cmake \
    libargtable2-dev \
    libsdl2-dev \
    libass-dev \
    ffmpeg
