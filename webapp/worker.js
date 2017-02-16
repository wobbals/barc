var https = require('https');
var validator = require('validator');
var config = require('config');
var kue = require('kue');
var queue = kue.createQueue();
var sleep = require('sleep');
var debug = require('debug')('barc:worker');

const child_process = require('child_process');
const fs = require('fs');
const zlib = require('zlib');

// queue.on('job enqueue', function(id, type){
//   console.log( 'Job %s got queued of type %s', id, type );
// });
//
// queue.inactive( function( err, ids ) {
//   // others are active, complete, failed, delayed
//   // you may want to fetch each id to get the Job object out of it...
//   console.log("ids: " + ids);
// });

process.once( 'SIGTERM', function ( sig ) {
  queue.shutdown( 5000, function(err) {
    console.log( 'Kue shutdown: ', err||'' );
    process.exit( 0 );
  });
});

queue.process('job', function(job, done) {
  debug("Received job " + job.id)
  if (job.data.archiveURL && validator.isURL(job.data.archiveURL)) {
    downloadArchive(job, job.data.archiveURL, function(result, error) {
      if (!error) {
        processArchive(job, done, result);
      } else {
        done("Failed to download archive");
      }
    });
  } else {
    debug("No archiveURL. Abort.");
    done("Missing archiveURL.");
  }
});

var processArchive = function(job, done, archiveLocalPath) {
  debug("Processing job " + job.id)
  var barc = config.get("barc_path");
  var cwd = process.cwd();
  debug("Working from " + cwd);
  debug(`job args: ` + JSON.stringify(job.data));
  var args = [];
  args.push(`-i${archiveLocalPath}`);
  args.push(`-o${cwd}/${job.id}.mp4`);
  if (job.data.width) {
    args.push("-w" + parseInt(job.data.width));    
  }
  if (job.data.height) {
    args.push("-h" + parseInt(job.data.height));
  }
  if (job.data.css_preset) {
    args.push("-p" + job.data.css_preset);
  }
  debug("spawn process " + barc);
  debug("args: ", args);
  
  // Note for nodemon users; this process creates files in the cwd. It will
  // kill your process without saying much and leave you proper confused.
  const child = child_process.spawn(barc, args, {
    detached: false,
    cwd: cwd
  });
  debug(`Spawned child pid ${child.pid}`)
  var logpath = `${cwd}/${job.id}.log`;
  var logfd = fs.openSync(logpath, "w+");
  
  child.stdout.on('data', function(data) {
    fs.write(logfd, data.toString(), function(err, written, string) {

    });
    var lines = data.toString().split("\n");
    lines.forEach(function(line) {
      try {
        var parsed = JSON.parse(line);
        if (parsed.progress) {
          job.progress(parsed.progress.complete, parsed.progress.total);
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
      done();
    } else {
      done(`Process exited with code ${code}`);
    }
    // finally, compress the log file and call it a day.
    var gzip = zlib.createGzip();
    const inp = fs.createReadStream(logpath);
    const out = fs.createWriteStream(`${logpath}.gz`);
    inp.pipe(gzip).pipe(out);
    fs.unlinkSync(logpath);
    if (!config.get("debugMode")) {
      // probably also good to clean up source archive if we're not debugging
      fs.unlinkSync(archiveLocalPath);      
    }
  });
}

var downloadArchive = function(job, archiveURL, callback) {
  debug(`Request download of archive ${archiveURL}`)
  var archivePath = `${job.id}-download`;
  var fd = fs.openSync(archivePath, "w+");
  var ws = fs.createWriteStream(null, { fd: fd , flags: 'w+' });
  var request = https.get(archiveURL, function(response) {
    debug("Archive download response: ", response.statusCode);
    var totalDownload = parseInt(response.headers['content-length'], 10);
    var currentDownload = 0;
    // pope all output to temporary file container
    response.pipe(ws);
    // keep job updated as download happens
    response.on("data", function(chunk) {
      currentDownload += chunk.length;
      job.progress(currentDownload, totalDownload);
    });
    response.on("end", function() {
      callback(archivePath, null);
    });
  }).on('error', function(err) {
    // Delete the file async. (But we don't check the result)
    debug("Error on archive download: ", err);
    fs.unlink(archivePath);
    if (callback) {
      callback(null, err.message); 
    }
  });
}