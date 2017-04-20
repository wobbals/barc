/*
 * This is a fork of worker.js from the barc webapp tree. Heading in a different
 * direction, this runner is charged with new environmental constraints:
 * - No config files: everything you need must be passed in via environment or
 *   Docker CMD args.
 * - Ephemeral: Process runs, dies, and the container is wiped away
 * - Stripped of Kue: scheduling moves from redis+kue to ECS tasks
 */

const child_process = require('child_process');
const fs = require('fs');
const zlib = require('zlib');
const path = require('path');

const request = require('request');
const progress = require('request-progress');
const validator = require('validator');
const s3 = require('s3');
var debug = require('debug')('barc:task');
const uploader = s3.createClient({
  maxAsyncS3: 20,     // this is the default
  s3RetryCount: 3,    // this is the default
  s3RetryDelay: 1000, // this is the default
  multipartUploadThreshold: 20971520, // this is the default (20 MB)
  multipartUploadSize: 15728640, // this is the default (15 MB)
  s3Options: {
    accessKeyId: process.env.S3_TOKEN,
    secretAccessKey: process.env.S3_SECRET,
    region: process.env.S3_REGION
  },
});

var processArchive = function(archiveLocalPath, requestArgs, cb) {
  debug(`begin processing task ${taskId}`)
  var barc = process.env.BARC_PATH || 'barc';
  var cwd = process.cwd();
  debug("Working from " + cwd);
  debug(`job args: ` + JSON.stringify(requestArgs));
  var archiveOutput = `${cwd}/${taskId}.mp4`;
  var args = [];
  args.push(`-i${archiveLocalPath}`);
  args.push(`-o${archiveOutput}`);
  if (requestArgs.width) {
    args.push("-w" + parseInt(requestArgs.width));    
  }
  if (requestArgs.height) {
    args.push("-h" + parseInt(requestArgs.height));
  }
  if (requestArgs.cssPreset) {
    args.push("-p" + requestArgs.cssPreset);
  }
  if (requestArgs.beginOffset) {
    args.push(`-b${requestArgs.beginOffset}`);
  }
  if (requestArgs.endOffset) {
    args.push(`-e${requestArgs.endOffset}`);
  }
  if (requestArgs.customCSS) {
    args.push(`-c${requestArgs.customCSS}`);
  }
  debug("spawn process " + barc);
  debug("args: ", args);

  // Note for nodemon users; this process creates files in the cwd. It will
  // kill your process without saying much and leave you well confused.
  const child = child_process.spawn(barc, args, {
    detached: false,
    cwd: cwd
  });
  debug(`Spawned child pid ${child.pid}`)
  var logpath = `${cwd}/${taskId}.log`;
  var logfd = fs.openSync(logpath, "w+");
  var last_progress = 0;
  child.stdout.on('data', function(data) {
    fs.write(logfd, data.toString(), function(err, written, string) {

    });
    var lines = data.toString().split("\n");
    lines.forEach(function(line) {
      try {
        var parsed = JSON.parse(line);
        if (parsed.progress) {
          var percentage =
          (100 * parsed.progress.complete / parsed.progress.total).toFixed(2);
          if (percentage - last_progress > 5) {
            debug(`Task progress ${percentage}%`);
            last_progress = percentage;
          }
        }
      } catch (e) {
        // just a line we can't parse. no biggie. 
      }
    });
  });
  child.stderr.on('data', function(data) {
    fs.write(logfd, data.toString(), function(err, written, string) {
      // nothing to do here, really.
      if (err) {
        debug("Error writing to log: ", err);
      }
    });
    //console.log(`stderr`);
    //console.log(data.toString());
  });
  child.on('exit', (code) => {
    debug(`Child exited with code ${code}`);
    if (0 == code) {
      cb(archiveOutput);
    } else {
      cb(null, `error - unknown return code ${code}`)
    }
    // finally, compress the log file and call it a day.
    var gzip = zlib.createGzip();
    const inp = fs.createReadStream(logpath);
    var compressed_logs = `${logpath}.gz`
    const out = fs.createWriteStream(compressed_logs);
    inp.pipe(gzip).pipe(out);
    out.on("finish", function() {
      uploadLogs(compressed_logs);
    });
    // clean up the mess we made during normal use
    fs.unlinkSync(logpath);
    if (process.env.CLEAN_ARTIFACTS) {
      // probably also good to clean up source archive if we're not debugging
      fs.unlinkSync(archiveLocalPath);
    }
  });
  child.on('error', function(err) {
    console.log("Spawn error " + err);
    cb(null, err);
  });
}

var uploadLogs = function(logpath) {
  if (!process.env.S3_PREFIX || !process.env.S3_BUCKET) {
    debug("Missing S3 configuration vars");
    return;
  }
  var key = `${process.env.S3_PREFIX}/${taskId}/${path.basename(logpath)}`;
  debug(`Upload job ${taskId} logs from ${logpath} to ${key} at ` +
    ` ${process.env.S3_BUCKET}`
  );
  const stats = fs.statSync(logpath);
  const fileSizeInBytes = stats.size;
  var params = {
    localFile: logpath,
    s3Params: {
      Bucket: process.env.S3_BUCKET,
      Key: key
    },
  };
  var upload = uploader.uploadFile(params);
  upload.on('error', function(err) {
    debug("unable to upload logs:", err.stack);
  });
  upload.on('progress', function() {
    // yay?
  });
  upload.on('end', function() {
    debug("done uploading logs");
    if (process.env.CLEAN_ARTIFACTS) {
      fs.unlinkSync(logpath);
    }
  });
};

var uploadArchiveOutput = function(archiveOutput, cb) {
  if (!process.env.S3_PREFIX || !process.env.S3_BUCKET) {
    debug("Missing S3 configuration vars");
    return;
  }
  var key = 
  `${process.env.S3_PREFIX}/${taskId}/${path.basename(archiveOutput)}`;
  debug(`Begin upload to ${key} at ${process.env.S3_BUCKET}`);
  var params = {
    localFile: archiveOutput,
    s3Params: {
      Bucket: process.env.S3_BUCKET,
      Key: key,
      ACL: 'private'
    },
  };
  var upload = uploader.uploadFile(params);
  upload.on('error', function(err) {
    debug("unable to upload:", err.stack);
    cb(null, err);
  });
  upload.on('progress', function() {
    // update job progress to keep from timing out
    // step 3: this phase should stay between 66% and 100%
    var normalizedComplete = upload.progressAmount + (2 * upload.progressTotal);
    var normalizedTotal = upload.progressTotal * 3;
    // TODO: This is another spot where progress updates need to get rewired
    // job.progress(normalizedComplete, normalizedTotal);
  });
  upload.on('end', function() {
    debug("done uploading archive");
    if (process.env.CLEAN_ARTIFACTS) {
      // clean up!
      fs.unlinkSync(archiveOutput);
    }
    debug(`task ${taskId} completed successfully.`);
    cb("success");
  });
}

var tryPostback = function(callbackURL, result) {
  if (!callbackURL || !validator.isURL(callbackURL)) {
    return;
  }
  var postback_options = {
    uri: callbackURL,
    method: 'POST',
    json: {
      job: `${taskId}`,
      result: result
    }
  };
  request(postback_options, function(error, response, body) {
    debug(`Postback to ${callbackURL} returned code ${response.statusCode}`);
  });
}

var downloadArchive = function(archiveURL, callback) {
  debug(`Request download of archive ${archiveURL}`)
  var archivePath = `${taskId}.archive.input`;
  progress(request(archiveURL), {
  })
  .on('progress', function (state) {
    // TODO: if the agent tracks job progress, we should forward this info
    //job.progress(state.size.transferred, state.size.total * 3);
  })
  .on('error', function (err) {
    debug("Error on archive download: ", err);
    fs.unlink(archivePath);
    if (callback) {
      callback(null, err.message); 
    }
  })
  .on('end', function () {
    callback(archivePath, null);
  })
  .pipe(fs.createWriteStream(archivePath));
}

/** MAIN TASK RUNNER */

const argv = require('minimist')(process.argv.slice(2));
console.dir(argv);
const taskId = process.env.TASK_ID;
const archiveURL = process.env.ARCHIVE_URL;
const callbackURL = process.env.CALLBACK_URL;

if (!archiveURL || !validator.isURL(archiveURL)) {
  debug(`fatal: '${archiveURL}' is not a URL. Set with env ARCHIVE_URL.`);
  return;
}

if (!taskId || !validator.isAscii(taskId)) {
  debug(`fatal: invalid task id ${taskId}. Set with env TASK_ID`);
  return;
}

downloadArchive(archiveURL, function(inputPath, error) {
  if (error) {
    debug(`Aborting task to error ${error}`);
    tryPostback(`download error: ${error}`);
    return;
  }
  processArchive(inputPath, argv, function(outputPath, error) {
    if (error) {
      debug(`Processing failed with error ${error}`);
      tryPostback(`process error: ${error}`);
      return;
    }
    uploadArchiveOutput(outputPath, function(result, error) {
      if (error) {
        tryPostback(`upload error: ${error}`);
      } else {
        tryPostback(result);
      }
    });
  });
});
