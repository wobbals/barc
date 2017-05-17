# Archive Manifest

## Standard usage

By design, barc supports consumption of archives produced by the [individual
stream archive](https://tokbox.com/developer/guides/archiving) mode.

## Static image media

In addition to the webm files that are specified in the `files` list of an
ordinary individual stream archive manifest, jpeg and png images are also
allowed with the same schema as videos. The same attributes of the file object
are required as with a webm audio/video file:

```json
"files": [
  {
    "filename" : "screenshot.png",
    "startTimeOffset" : 0,
    "stopTimeOffset" : 10000,
    "streamId" : "2e9dd12c-3b49-11e7-bf38-1f67032b74ef",
    "videoType" : "screen"
  }
]
```

Note that `streamId` is also a required attribute. This is needed in order to
set a unique identifier in the DOM of the CSS layout. Any unique string should
work fine.

See also discussion on this issue in #12.

