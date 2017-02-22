# Batch Archive Processing

Feed me an individual stream archive, I'll process it on my own time.


# Building

Build using CMake. In addition to cmake you will need:

* cmake 3.5 or higher
* clang (recommended)
* ffmpeg/libav libraries (including, not limited to: libavcodec, libavfilter,
  etc.)
  * ffmpeg will need some form of H.264 encoder. x264 is a great chioce,
    but comes with some licensing concerns under GPLv2. OpenH264 should work,
    but has not been tested.
  * ffmpeg also needs libopus to work with TokBox archives. The built in opus
    codec *will not do* in certain cases.
* libopus
* libuv
  * ubuntu package `apt-get install libuv1 libuv1-dev`
  * min version 1.11
* libjansson (ubuntu package `apt-get install libjansson4 libjansson-dev`)
* libzip 
  * ubuntu package `apt-get install libzip4 libzip-dev`
  * min version 1.1
* magickwand 7 (of ImageMagick fame) 
  * Version 6 might work, but is untested.

For each of these packages, we use pkg-config to autodiscover linker and 
compiler flags. If `pkg-config --libs libavcodec` does not return anything
meaningful on your machine, you can expect the build to fail.

Once all this software is installed, use

```sh
mkdir build && cd build
cmake ..
make
```

Binary will be available in your `build` directory. Have at it!

# Usage

## CLI tool

```sh
./barc [options]
```

options include:

* `-i input` - input zip or working directory (required)
* `-o output` - output file - recommended to use `*.mp4`; although avformat
  will try to use whatever it's given, there may be some assumptions in the 
  code that need to be fixed in order to support other container formats.
  (default: `output.mp4`)
* `-p preset` - preset CSS for output layout. one of `bestFit`, 
  `horizontalPresentation`, `verticalPresentation`, or `custom`.
  (default: `bestFit`)
* `-w width` - output container width (default 640)
* `-h height` - output container height (default 480)
* `-c css` - custom css for output layout. Option is ignored if `-p custom` is
  not also passed.
* `-b beginOffset` - offset start time in seconds
* `-e endOffset` - offset stop time in seconds
  
## Input ZIP / directory

* Input directory or zip is assumed to be working from the context of an
  individual stream archive.
* If a directory is provided with `-i input`, this will be used as the working
  directory during the run. A relative path for `-o output` will be relative
  to this working directory.
* If a zip is provided with `-i input`, the current working directory of the
  shell that started the process will be used, adding `out/` as a subdirectory
  and expanding the zip to this directory.

  
## Archive manifest

* Additional metadata may be added to a stream attribute of the archive
  manifest.
* Use `layoutClass` to specify a layout class for a stream. For example:

```json
{
   "createdAt" : 1486583789883,
   "files" : [
      {
         "connectionData" : "{\"userName\":\"Bob\"}",
         "filename" : "96e68d34-300b-48df-9990-7ca1ad613a6f.webm",
         "size" : 56313861,
         "startTimeOffset" : 672488,
         "stopTimeOffset" : 3458599,
         "streamId" : "96e68d34-300b-48df-9990-7ca1ad613a6f",
         "layoutClass": "focus"
      },
  ...
}
```

## Archive layout presets

Using `horizontalPresentation` or `verticalPresentation`, you can specify an
important video stream that should get more screen space on the composed output.
Specify a `layoutClass` as an attribute of the stream in your archive manifest,
and give it the string value `focus`. CSS stylesheets and classes can also be
defined by the custom CSS input parameter, allowing you to create your own
layouts. 
Please, if you have a layout class that you think should be a preset, raise an
issue with the stylesheet!

# TODO

* Audio resampling and filtering to allow ambiguous input/output formats
* audio-only overlay / indicator
* Dynamic class changes
* Dynamic stylesheet changes
* Scale mode switching
* image effect chains (ex. vignette)
