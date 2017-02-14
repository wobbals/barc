# Batch Archive Processing

Feed me an individual stream archive, I'll process it on my own time.


# Building

Build using CMake. In addition to cmake you will need:

* ffmpeg/libav libraries (including, not limited to: libavcodec, libavfilter,
  etc.)
..* ffmpeg will need some form of H.264 encoder. x264 is a great chioce,
    but comes with some licensing concerns under GPLv2.
..* ffmpeg also needs libopus to work with TokBox archives. The built in opus
    codec _will not do_.
* libopus
* libuv (ubuntu package `apt-get install libuv1 libuv1-dev`)
* libjansson (ubuntu package `apt-get install libjansson4 libjansson-dev`)
* libzip (ubuntu package `apt-get install libzip4 libzip-dev`)
* magickwand 7 (of ImageMagick fame) - version 6 might work, but is untested.

For each of these packages, we use pkg-config to autodiscover the needed linker
and compiler flags. If `pkg-config --libs libavcodec` does not return anything
meaningful for you, you can expect the build to fail.

Once all this software is installed, use

```sh
mkdir build && cd build
cmake ..
make
```

Binary will be available in your `build` directory. Have at it!

# TODO

* Audio resampling and filtering to allow ambiguous input/output formats
* audio/video-only support
* Dynamic class changes
* Dynamic CSS changes
