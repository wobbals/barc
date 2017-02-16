var express = require('express');
var router = express.Router();
var kue = require('kue');
var job_queue = kue.createQueue();
var job_helper = require("../helpers/job_helper");
var debug = require('debug')('webapp:index');
var config = require('config');

router.get('/', function(req, res, next) {
  res.render('index', { title: 'Express' });
});

router.post('/job', function(req, res) {
  var job_args = job_helper.parseJobArgs(req.body)
  if (job_args.error) {
    res.json(job_args);
    return; 
  }
  var job = job_queue.create("job", job_args)
  .ttl(config.get("job_defaults.ttl"))
  .save( function(err){
    if( !err ) {
      res.json({ "job_id": job.id });
    } else {
      res.json({})
    }
  });
});

router.get('/job/:id', function(req, res) {
  
});

module.exports = router;
