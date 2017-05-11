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
var Job = require('../model/job');
var request = require('request');

var tryPostback = function(callbackURL, message) {
  if (!callbackURL || !validator.isURL(callbackURL)) {
    debug(`tryPostback: invalid URL ${callbackURL}`);
    return;
  }
  var postback_options = {
    uri: callbackURL,
    method: 'POST',
    json: message
  };
  debug(`tryPostback: ${JSON.stringify(postback_options)}`);
  request(postback_options, function(error, response, body) {
    debug(`Postback to ${callbackURL} returned code ${response.statusCode}`);
  });
}

var tryExternalPostback = async function(taskId, message) {
  let job;
  try {
    job = await Job.getJob(taskId);
  } catch (e) {
    debug(`tryExternalPostback: `, e);
    return;
  }
  if (!job) {
    debug(`tryExternalPostback: no job ${taskId}`);
    return;
  }
  tryPostback(job.externalCallbackURL, message)
}

var handlePostback = async function(body) {
  debug(`handlePostback:`, body);
  if (!body.message || !body.taskId) {
    return;
  }
  let taskId = body.taskId;
  let message = body.message;
  let jobData = {
    lastMessage: new Date().getTime()
  };
  if (message.output_key) {
    jobData.archiveKey = message.output_key;
  }
  if (message.output_bucket) {
    jobData.archiveBucket = message.output_bucket;
  }
  if (message.logs_key) {
    jobData.logsKey = message.logs_key;
  }
  if (message.logs_bucket) {
    jobData.logsBucket = message.logs_bucket;
  }
  if (message.error) {
    jobData.error = JSON.stringify(message.error);
  }
  if (message.status) {
    jobData.status = message.status;
  }
  if (message.progress) {
    jobData.progress = message.progress;
  }
  debug(`handlePostback: persist job data`, jobData);
  try {
    await Job.persist(taskId, jobData);
  } catch (e) {
    debug('handlePostback:', e);
    debug(e.stack);
  }
  if (message.status) {
    tryExternalPostback(taskId, {status: message.status, jobId: taskId});
  }
}
module.exports.handlePostback = handlePostback;

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

  if (args.callbackURL && validator.isURL(args.callbackURL)) {
    result.externalCallbackURL = args.callbackURL;
  }
  // intercept old external callback URL with our own internal endpoint
  // TODO: move to config
  result.callbackURL = config.get('internal_callback_base_url') +
  '/job/callback';

  return result;
}

/* To be compatible with JS minimist, please use longopt format */
var taskize = function(requestArgs) {
  let args = ['node', 'task.js'];
  if (requestArgs.width) {
    args.push(`--width`);
    args.push(`${parseInt(requestArgs.width)}`);
  }
  if (requestArgs.height) {
    args.push(`--height`);
    args.push(`${parseInt(requestArgs.height)}`);
  }
  if (requestArgs.cssPreset) {
    args.push(`--css_preset`);
    args.push(`${requestArgs.cssPreset}`);
  }
  if (requestArgs.beginOffset) {
    args.push('--begin_offset');
    args.push(`${requestArgs.beginOffset}`);
  }
  if (requestArgs.endOffset) {
    args.push(`--end_offset`);
    args.push(`${requestArgs.endOffset}`);
  }
  if (requestArgs.customCSS) {
    args.push(`--custom_css`);
    args.push(`"${requestArgs.customCSS}"`);
  }
  return args;
}
module.exports.taskize = taskize;

var validateJobToken = function(job, token) {
  var calculated_secret = hash_generator.calc(token,
    config.get("secret_token_length"),
    config.get("secret_token_salt")
  );
  return (job.data.secret === calculated_secret);
}

var getJobDownloadURL = function(key) {
  return new Promise((resolve, reject) => {
    var params = {
      Bucket: config.get("s3_bucket"),
      Key: key,
      Expires: 600 // 10 minutes
    };
    s3_client.getSignedUrl('getObject', params, function (err, url) {
      if (err) {
        debug(`getJobDownloadURL: `, err);
        reject(err);
      } else {
        resolve(url);
      }
    });
  });
}

module.exports.getJobDownloadURL = getJobDownloadURL;
module.exports.validateJobToken = validateJobToken;
module.exports.parseJobArgs = parseJobArgs;