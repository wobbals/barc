# Barc web interface

## Overview

This webapp provides a web programmable interface to the barc CLI utility.

## Running

* Install deps with `npm install`
* Launch a redis server
* Start the webserver with `npm start`
* Start a worker process (or many) with `npm run worker`. One worker will spawn
  one barc process, and work on the queue serially.

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
 \"css_preset\": \"horizontalPresentation\"}" \
 http://localhost:3000/job
```


## Monitoring job progress

API is not yet implemented, but with `debugMode` enabled in the config, you can 
visit `localhost:3001` to monitor job progress.
