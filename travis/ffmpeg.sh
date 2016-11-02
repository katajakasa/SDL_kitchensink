
if [ ! -e "$HOME/local/lib/libavcodec.so" ]; then 
    wget https://www.ffmpeg.org/releases/ffmpeg-3.0.4.tar.gz -O ~/ffmpeg.tar.gz
    tar xzf ~/ffmpeg.tar.gz -C ~/
    cd ~/ffmpeg-3.0.4
    export CC=gcc-5
    ./configure --prefix=$HOME/local --disable-static --enable-shared --disable-doc
    make
    make install
else 
    echo 'Using cached FFmpeg build directory.';
fi
