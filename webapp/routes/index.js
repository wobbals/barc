var express = require('express');
var router = express.Router();
var debug = require('debug')('webapp:index');
var config = require('config');
var hash_generator = require('random-hash-generator');
var kue = require('kue');
var kue_opts = {
  redis: {
      host: process.env.redis_host || config.get("redis_host"),
      port: config.has("redis_port") ? config.get("redis_port") : 6379,
  }
};
var job_queue = kue.createQueue(kue_opts);
var job_helper = require("../helpers/job_helper");
var kennel = require("../helpers/kennel");
var Job = require('../model/job');

router.get('/', function(req, res, next) {
  res.render('index', { title: 'BARC' });
});

router.post('/v2/job', function(req, res) {
  let job_args = job_helper.parseJobArgs(req.body);
  if (job_args.error) {
    res.json(job_args);
    res.status(400);
    return;
  }
  let key_pair = hash_generator.generate(
    config.get("secret_token_length"),
    config.get("secret_token_length"),
    config.get("secret_token_salt")
  );
  let job_data = {};
  job_data.secret = key_pair.secret;
  if (job_args.externalCallbackURL) {
    job_data.externalCallbackURL = job_args.externalCallbackURL;
  }
  kennel.postTask(job_args, (error, response) => {
    if (error) {
      res.status(500);
      res.json({error: error});
    } else {
      Job.persist(response.taskId, job_data);
      res.status(202);
      res.json({job_id: response.taskId, access_token: key_pair.key});
    }
  });
});

router.get('/v2/job/:id', async function(req, res) {
  let tokenValidated = await Job.checkKey(req.params.id, req.query.token);
  if (!tokenValidated) {
    res.status(403).json({"error": "missing or invalid token"});
    return;
  }
  kennel.getTask(req.params.id, function(err, body) {
    if (err) {
      res.status(404).json({"error": `unknown job ${req.params.id}`});
      return;
    }
    res.json(body);
  });
});

router.get('/v2/job/:id/download', async function(req, res) {
  var redirect = (req.query.redirect === "true");
  let job;
  try {
    job = await Job.getJob(req.params.id);
  } catch (e) {
    debug(e);
    return res.status(404).json({"error": `unknown job ${req.params.id}`});
  }
  debug('download job', job);
  let tokenValidated = await Job.checkKey(req.params.id, req.query.token);
  if (!tokenValidated) {
    res.status(403).json({"error": "missing or invalid token"});
    return;
  }
  if (job.status !== "success") {
    res.status(202).json({
      "message": `job status ${job.status}. try again later.`
    });
    return;
  }
  if (!job.archiveKey || job.archiveBucket !== config.get("s3_bucket")) {
    res.status(409).json({error: 'this server has no access to job archive'});
    return;
  }
  let downloadURL = null;
  try {
    downloadURL = await job_helper.getJobDownloadURL(job.archiveKey);
  } catch (e) {
    debug(e);
  }
  if (!downloadURL) {
    res.status(500).json({error: 'failed to fetch download url'});
  } else if (redirect) {
    res.redirect(downloadURL);
  } else {
    res.status(200).json({"downloadURL": downloadURL});
  }
});

// TODO: This function needs some form of access control
router.post('/v2/job/callback', (req, res) => {
  job_helper.handlePostback(req.body);
  res.status(204);
});

router.post('/job', function(req, res) {
  var job_args = job_helper.parseJobArgs(req.body)
  if (job_args.error) {
    res.json(job_args);
    return;
  }
  var key_pair = hash_generator.generate(
    config.get("secret_token_length"),
    config.get("secret_token_length"),
    config.get("secret_token_salt")
  );
  job_args.secret = key_pair.secret;
  var job = job_queue.create("job", job_args)
  .ttl(config.get("job_defaults.ttl"))
  .save( function(err){
    if( !err ) {
      res.json({ "job_id": job.id, "access_token": key_pair.key });
    } else {
      res.json({})
    }
  });
});

router.get('/job/:id', function(req, res) {
  kue.Job.get(req.params.id, function(err, job) {
    if (err) {
      res.status(404).json({"error": `unknown job ${req.params.id}`});
      return;
    }
    if (job_helper.validateJobToken(job, req.query.token)) {
      res.json(job_helper.getJobStatus(job, job_queue));
    } else {
      res.status(403).json({"error": "missing or invalid token"});
    }
  });
});

router.get('/job/:id/download', function(req, res) {
  var redirect = (req.query.redirect === "true");
  kue.Job.get(req.params.id, function(err, job) {
    if (err) {
      res.status(404).json({"error": `unknown job ${req.params.id}`});
      return;
    }
    if (!job_helper.validateJobToken(job, req.query.token)) {
      res.status(403).json({"error": "missing or invalid token"});
      return;
    }
    if (job.state() !== "complete") {
      res.status(202).json({
        "message": `job status ${job.state()}. try again later.`
      });
      return;
    }
    var downloadURL = job_helper.getJobDownloadURL(job, res, redirect);
  });
});

module.exports = router;
