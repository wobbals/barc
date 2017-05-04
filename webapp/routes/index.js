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
  var job_args = job_helper.parseJobArgs(req.body);
  if (job_args.error) {
    res.json(job_args);
    res.status(400);
    return;
  }
  var key_pair = hash_generator.generate(
    config.get("secret_token_length"),
    config.get("secret_token_length"),
    config.get("secret_token_salt")
  );
  Job.persist(job_args, key_pair.secret);
  kennel.postTask(job_args, (error, response) => {
    if (error) {
      res.status(500);
      res.json({error: error});
    } else {
      res.status(202);
      res.json({job_id: response.taskId, access_token: key_pair.key});
    }
  });
});

router.get('/v2/job/:id', (req, res) => {
  kennel.getTask(req.params.id, function(err, body) {
    if (err) {
      res.status(404).json({"error": `unknown job ${req.params.id}`});
      return;
    }
    debug(`wtf:`, body);
    res.status(200);
    res.json(body);
    // TODO: re-add token validation
    // if (job_helper.validateJobToken(job, req.query.token)) {
    //   res.json(job_helper.getJobStatus(job, job_queue));
    // } else {
    //   res.status(403).json({"error": "missing or invalid token"});
    // }
  });
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
