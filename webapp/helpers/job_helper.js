var debug = require('debug')('webapp:job_helper');
var validator = require('validator');
var config = require('config');
var hash_generator = require('random-hash-generator');
var AWS = require('aws-sdk');
var s3_client = new AWS.S3({
    accessKeyId: config.get("aws_token"),
    secretAccessKey: config.get("aws_secret"),
    region: config.get("s3_region")
});

var parseJobArgs = function(args) {
  var result = {}
  
  // required parameters first
  if (args.archiveURL && validator.isURL(args.archiveURL, {
    protocols: ['http','https']
  })) {
    result.archiveURL = validator.stripLow(args.archiveURL);
  } else {
    result.error = "Missing required parameter: archiveURL"
    return result;
  }
  
  if (args.cssPreset && 
    config.get("known_css_presets").indexOf(args.cssPreset) > -1)
  {
    result.cssPreset = args.cssPreset;
  }

  if (result.cssPreset == "custom") {
    result.customCSS = validator.stripLow(args.customCSS);
  }
  
  if (validator.isInt(args.width + '', { 
    min: config.get("job_limits.min_width"),
    max: config.get("job_limits.max_width")
  })) {
    result.width = parseInt(args.width);
  } else {
    result.width = config.get("job_defaults.width");
  }
  
  if (validator.isInt(args.height + '', { 
    min: config.get("job_limits.min_height"),
    max: config.get("job_limits.max_height")
  })) {
    result.height = parseInt(args.height);
  } else {
    result.height = config.get("job_defaults.height");
  }
  
  if (validator.isInt(args.beginOffset + '')) {
    result.beginOffset = parseInt(args.beginOffset);
  }
  
  if (validator.isInt(args.endOffset + '')) {
    result.endOffset = parseInt(args.endOffset);
  }
  
  if (validator.isURL(args.callbackURL)) {
    result.callbackURL = args.callbackURL;
  }
  
  return result;
}

var validateJobToken = function(job, token) {
  var calculated_secret = hash_generator.calc(token, 
    config.get("secret_token_length"),
    config.get("secret_token_salt")
  );
  return (job.data.secret === calculated_secret);
}

var getJobStatus = function(job, queue) {
  var result = {};
  result.job_id = job.id;
  var job_state = job.state();
  if (job_state === "inactive") {
    result.status = "queued";
  } else if (job_state === "active") {
    result.status = "running";
  } else {
    result.status = job_state;
  }
  if (job.error()) {
    result.error = job.error();
  }
  result.created_at = job.created_at;
  result.updated_at = job.updated_at;
  if (job.failed_at) {
    result.failed_at = job.failed_at;
  }
  result.progress = job.progress();
  return result;    
}

var getJobDownloadURL = function(job, res, redirect) {
  var params = { 
    Bucket: config.get("s3_bucket"), 
    Key: job.result.s3_key,
    Expires: 600 // 10 minutes
  };
  s3_client.getSignedUrl('getObject', params, function (err, url) {
    if (redirect) {
      res.redirect(url);
    } else {
      res.status(200).json({"downloadURL": url});
    }
  });
}

module.exports.getJobDownloadURL = getJobDownloadURL;
module.exports.getJobStatus = getJobStatus;
module.exports.validateJobToken = validateJobToken;
module.exports.parseJobArgs = parseJobArgs;