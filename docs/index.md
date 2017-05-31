# Barc: A batch archive composer

Individual stream archives require some post processing in order to be
consumable to end users. Barc is a tool for doing just that -- it can consume
an individual stream archive and produce a single composition container of the
media from the archive. Features include:

* Support for high-definition composed archives
* CSS-based layout management
  * Several predefined styles available for you to choose from
  * Custom stylesheets for complete customizability
* Simple web API and job queue for running a batch processing server
*

## CSS layout presets

For full documentation on layout usage, see [Stylesheets](Stylesheets.md).

## Additional manifest features

Like the secret menu at your favorite restaurant, some extra features are
available to you by manipulating the [archive manifest](Manifest.md)
before starting a barc job.

## Usage: Local

See [Developers](Developers.md)

## Usage: web

See [WebAPI](WebAPI.md)

A demo server is available for OpenTok customers. To process an individual
stream archive, send an HTTP POST to https://kennel.wobbals.com/barc/job with
the parameters of your job:

```js

var request = require('request');

var body = {
  width: 640,
  height: 480,
  archiveURL: "https://example.com/archive.zip",
  cssPreset: "auto",
  callbackURL: "https://example.com/callback",
  version: "breckenridge"
};
var myJobId;

request.post({
  url: `${barcURL}/job`,
  json: body
}, (error, response, body) => {
  if (error) {
    console.log(error);
  } else {
    myJobId = body.jobId;
  }
});

```
