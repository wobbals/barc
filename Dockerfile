# This is the Dockerfile for the barc worker, which attaches to the
# job queue managed in the `webapp/` directory.
FROM ubuntu:latest

# grab first dependencies from apt
RUN apt-get update
RUN apt-get install -y cmake libuv1 libuv1-dev libjansson4 libjansson-dev \
libzip4 libzip-dev git clang automake autoconf libx264-dev libopus-dev yasm \
pkg-config curl 

RUN curl -sL https://deb.nodesource.com/setup_6.x | bash -
RUN apt-get install -y nodejs

# Should only need this for debugging. Probably safe to remove.
# RUN apt-get install -y net-tools

# Create app directory
RUN mkdir -p /var/lib/barc/ext
WORKDIR /var/lib/barc/ext

# Build deps: 1 of 2 - ImageMagick

RUN git clone https://github.com/ImageMagick/ImageMagick.git
WORKDIR /var/lib/barc/ext/ImageMagick

RUN git checkout 7.0.4-5
RUN ./configure
RUN make
RUN make install

# Build deps: 2 of 2 - FFmpeg

WORKDIR /var/lib/barc/ext
RUN git clone https://github.com/FFmpeg/FFmpeg.git
WORKDIR /var/lib/barc/ext/FFmpeg
RUN git checkout n3.2.4

RUN ./configure --enable-libx264 --enable-gpl \
  --extra-ldflags=-L/usr/local/lib \
  --extra-cflags=-I/usr/local/include \
  --enable-libopus
RUN make
RUN make install

WORKDIR /var/lib/barc

# Copy app source
COPY CMakeLists.txt /var/lib/barc/CMakeLists.txt
COPY barc /var/lib/barc/barc
COPY ext /var/lib/barc/ext

# build barc binary
RUN mkdir -p /var/lib/barc/build
WORKDIR /var/lib/barc/build
RUN cmake ..
RUN make

# Copy webapp (for worker.js)
COPY webapp /var/lib/barc/webapp
WORKDIR /var/lib/barc/webapp
RUN npm install

ENV PATH=${PATH}:/var/lib/barc/build
ENV DEBUG=barc:worker
ENV LD_LIBRARY_PATH=/usr/local/lib

CMD [ "npm", "run", "worker" ]
