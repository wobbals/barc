# Barc web interface

## Overview

This webapp provides a web programmable interface to the barc CLI utility.

## Running

### Directly

* Install deps with `npm install`
* Launch a redis server
* Start the webserver with `npm start`
* Start a worker process (or many) with `npm run worker`. One worker will spawn
  one barc process, and work on the queue serially.

### From Docker

* `docker-compose up --build -d`

## Creating jobs

Inputs mirror the inputs of the barc CLI tool. To schedule a job, send a POST
to `/jobs`. The only required parameter is `archiveURL`, which must be reachable
by the worker. Note that jobs might not run immediately, so S3 tokens should
have a lenient expiration time.

```sh
curl -v \
-H "Content-Type: application/json" \
-d "{\"width\": \"1280\", \
 \"height\": \"720\", \
 \"archiveURL\": \"https://example.com/archive\", \
 \"css_preset\": \"horizontalPresentation\" }" \
 http://localhost:3000/barc/job
```

If a job is accepted to the service queue, you will receive a job ID and access
token for fetching job progress and results in the future.
**Do not lose this data**.

```json
{"job_id":"24f7e00c-8b83-458f-8309-d7fbc47dfed5","accessToken":"asdf1234"}
```

### Acceptable job arguments

New jobs can use the same arguments as the CLI tool. These include:

* `width`, `height` - output dimensions of the processed container.
* `archiveURL` -- this is where the worker will go fetch your individual stream
  archive. These archives must be in the same format as provided by OpenTok.
* `cssPreset` -- one of the presets defined in the command line utility.
* `customCSS` -- if `custom` is used in the `css_preset` argument, then you
  should specify a CSS stylesheet string here.
* `beginOffset` -- Seeks forward in the output a number of seconds before
  beginning to process. Useful for skipping content you don't wish to keep.
* `endOffset` -- Truncates output to this timestamp. Useful for creating faster
  jobs during testing or skipping content you do not wish to keep.
* `callbackURL` -- A URL where the worker will send a POST after the job is
  completed. Request body will be JSON of the form:
  `{"jobId":"24f7e00c-8b83-458f-8309-d7fbc47dfed5","status":"complete"}`

## Monitoring job progress

The recommended method for getting job status is by providing `callbackURL` in
the request to schedule the job. That said, sometimes it's necessary to
explicitly check for the progress of a job:

`GET /barc/job/:id`
Gets the status of the job.

Query paremeters:
* `token` -- required. This was given to when the job was scheduled.

Response keys:
* `status`: the status of the job, as reported by the encoder
  - `accepted`: the request for this job was valid and is waiting for an
    instance to run
  - `launched`: the task runner for this job has started and is healthy
  - `processing`: the actual processing job has begun on the task runner. this
  happens after archive download is complete.
  - `uploading`: the processor has exited and archive is being uploaded
  - `complete`: job completes successfully
  - `error`: something bad happened. usually an `error` key will be available.
* `progress`: a number between 0 and 100, representing the estimated completion.
* `createdAt`: timestamp when this job was passed to the cluster
* `startedAt`: timestamp when this job began running on the cluster
* `stoppedAt`: timestamp when this job stopped running on the cluster


### Example

```sh
 curl -s "http://localhost:3000/barc/job/1234abcd?token=asdf"

{"job_id":"123","status":"complete","progress":"100"}

```

## Fetching job results

`GET /job/:id/download`

Query parameters:

* `token` -- required. This was given to when the job was scheduled.
* `redirect` -- default: false. Set to `true` to get a 302 to the temporary
  URL for download. Otherwise, fetch the temporary url from the response body
  JSON with key `downloadURL`.

###Examples

```sh
 curl -s "http://localhost:3000/barc/job/1234abcd/download?token=asdf&redirect=true"
 # no response

curl -v "http://localhost:3000/barc/job/1234abcd/download?token=asdf&redirect=false"

{"downloadURL":"https://s3-us-west-2.amazonaws.com/tb-charley-test.tokbox.com/barc/123/123.mp4?AWSAccessKeyId=AKIAIU6WAU4PRYRHZXFQ&Expires=1487996736&Signature=Pv%2Br6yXzO4QmUIHjXuaH1mepElY%3D"}
```
