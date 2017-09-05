# This is the Dockerfile for the barc worker, which attaches to the
# job queue managed in the `webapp/` directory.
FROM ubuntu:latest

# grab first dependencies from apt
RUN apt-get update && \
apt-get install -y cmake libuv1 libuv1-dev libjansson4 libjansson-dev \
libzip4 libzip-dev git clang automake autoconf libx264-dev libopus-dev yasm \
pkg-config curl libcurl4-gnutls-dev && \
curl -sL https://deb.nodesource.com/setup_8.x | bash - && \
apt-get install nodejs && rm -rf /var/lib/apt/lists/*

# Create app directory
RUN mkdir -p /var/lib/barc/ext
WORKDIR /var/lib/barc/ext

# Build deps: 1 of 2 - ImageMagick

RUN git clone https://github.com/ImageMagick/ImageMagick.git && \
cd ImageMagick && \
git checkout 7.0.4-5 && \
./configure '--with-png=yes' '--with-jpeg=yes' && \
make && \
make install && \
cd .. && rm -rf ImageMagick

# Build deps: 2 of 2 - FFmpeg

WORKDIR /var/lib/barc/ext
RUN git clone https://github.com/FFmpeg/FFmpeg.git && \
cd FFmpeg && \
git checkout n3.2.4 && \
./configure --enable-libx264 --enable-gpl \
  --extra-ldflags=-L/usr/local/lib \
  --extra-cflags=-I/usr/local/include \
  --enable-libopus && \
make && \
make install && \
cd .. && rm -rf FFmpeg

WORKDIR /var/lib/barc

# Copy app source
COPY CMakeLists.txt /var/lib/barc/CMakeLists.txt
COPY barc /var/lib/barc/barc
COPY test /var/lib/barc/test
COPY ext /var/lib/barc/ext

# build barc binary
RUN mkdir -p /var/lib/barc/build /var/lib/barc/bin && \
cd /var/lib/barc/build && \
cmake .. && \
make && mv barc ../bin && rm -rf *

# Copy webapp (for worker.js)
COPY webapp /var/lib/barc/webapp
WORKDIR /var/lib/barc/webapp

RUN npm install

ENV PATH=${PATH}:/var/lib/barc/bin
ENV DEBUG=barc:worker
ENV LD_LIBRARY_PATH=/usr/local/lib

CMD [ "npm", "run", "worker" ]
