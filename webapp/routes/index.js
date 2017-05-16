var express = require('express');
var router = express.Router();
var debug = require('debug')('webapp:index');
var config = require('config');
var hash_generator = require('random-hash-generator');
var job_helper = require("../helpers/job_helper");
var kennel = require("../helpers/kennel");
var Job = require('../model/job');

router.get('/', function(req, res, next) {
  res.render('index', { title: 'BARC' });
});

router.post('/job', function(req, res) {
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
  kennel.postTask(req.body, (error, response) => {
    if (error) {
      res.status(error.code ? error.code : 500);
      res.json({error: error});
    } else {
      job_data.status = 'queued';
      Job.persist(response.taskId, job_data);
      res.status(202);
      res.json({jobId: response.taskId, accessToken: key_pair.key});
    }
  });
});

router.get('/job/:id', async function(req, res) {
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

router.get('/job/:id/download', async function(req, res) {
  var redirect = (req.query.redirect === "true");
  let job = null;
  try {
    job = await Job.getJob(req.params.id);
  } catch (e) {
    debug(e);
  }
  if (!job) {
    return res.status(404).json({"error": `unknown job ${req.params.id}`});
  }
  debug('download job', job);
  let tokenValidated = await Job.checkKey(req.params.id, req.query.token);
  if (!tokenValidated) {
    res.status(403).json({"error": "missing or invalid token"});
    return;
  }
  if (job.status !== "complete") {
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

module.exports = router;
