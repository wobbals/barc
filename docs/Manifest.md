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

## Dynamic layout changes

Archive CSS stylesheet and individual stream classes can be modified throughout
the duration of the archive with the addition of the `layoutEvents` array.
API events may included in this array as object entries.
All entries have `action` and `createdAt` attributes.
For `action: layoutChanged`, a `layout` object is included, and
mirrors the schema defined in the
[layout management API](https://tokbox.com/developer/beta/archive-custom-layout/).
For `action: streamChanged`, a `stream` object is included,
and mirrors the stream resource from the same API.

```json

{
   "createdAt" : 1486407755924,
   "files" : [
     ...
   ],
   "layoutEvents" : [
     {
       "createdAt" : 1486407755924,
       "action" : "layoutChanged",
       "layout":
       {
         "type": "custom",
         "stylesheet": ".instructor { width: 100%;  height:50%; }"
       }
     },
     {
       "createdAt" : 1486407760000,
       "action" : "streamChanged",
       "stream" :
       {
         "id": "8b732909-0a06-46a2-8ea8-074e64d43422",
         "videoType": "camera",
         "layoutClassList": ["instructor", "above"],
       }
     },
     {
       "createdAt" : 1486407768500,
       "action" : "layoutChanged",
       "layout":
       {
         "type": "pip"
       }
     },
   ],
   "id" : "e698c4f7-2cda-450e-828f-7f2ecac7abbd",
   "name" : "",
   "sessionId" : "1_MX40NTYyNTEyMn5-MTQ4NjQwNzYxNDAzNH5yN2V4OCs2NTk1WEZ3QjF2dy9ISFZUMnN-fg"
}
```

See also discussion on this issue in #4

