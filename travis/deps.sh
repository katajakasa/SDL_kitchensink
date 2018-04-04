sudo add-apt-repository -y ppa:zoogie/sdl2-snapshots
sudo add-apt-repository -y ppa:george-edison55/cmake-3.x
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo add-apt-repository -y ppa:jonathonf/ffmpeg-3
sudo apt-get update
sudo apt-get -y install \
    gcc-7 \
    cmake \
    libargtable2-dev \
    libsdl2-dev \
    libass-dev \
    ffmpeg
