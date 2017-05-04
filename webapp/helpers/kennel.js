const request = require('request');
const config = require('config');
const jobHelper = require('./job_helper');
const debug = require('debug')('barc:kennel');
const validator = require('validator');

const postTask = function(taskArgs, cb) {
  let args = jobHelper.parseJobArgs(taskArgs);
  let taskBody = {};
  taskBody.task = config.get('kennel.task');
  taskBody.container = config.get('kennel.container');
  taskBody.command = jobHelper.taskize(args);
  taskBody.environment = {};
  taskBody.environment.S3_SECRET = config.get('aws_secret');
  taskBody.environment.S3_TOKEN = config.get('aws_token');
  taskBody.environment.S3_BUCKET = config.get('s3_bucket');
  taskBody.environment.S3_PREFIX = config.get('s3_prefix');
  taskBody.environment.S3_REGION = config.get('s3_region');
  taskBody.environment.ARCHIVE_URL = args.archiveURL;
  taskBody.environment.CALLBACK_URL = args.callbackURL;
  taskBody.environment.DEBUG = '*.*';

  debug(`task: ${JSON.stringify(taskBody, null, ' ')}`);

  request.post({
    url: `${config.get('kennel_base_url')}/task`,
    json: taskBody
  }, (error, response, body) => {
    debug('error:', error); // Print the error if one occurred
    debug('statusCode:', response && response.statusCode); // Print the response status code if a response was received
    debug('body:', body); // Print the HTML for the Google homepage.

    cb(error, body)
  });
};
module.exports.postTask = postTask;

const getTask = function(taskId, cb) {
  if (!validator.isUUID(taskId)) {
    cb({error: `invalid taskId ${taskId}`});
    return;
  }
  request.get({
    url: `${config.get('kennel_base_url')}/task/${taskId}`
  }, (error, response, bodyStr) => {
    let body = JSON.parse(bodyStr);
    debug(`kennel get task response: ${JSON.stringify(response, null, ' ')}`);
    debug(`kennel get task body: ${JSON.stringify(body, null, ' ')}`);
    if (error) {
      debug(`kennel get task error: ${error}`);
      cb({error: 'internal error: kennel query failed'});
      return;
    }
    let result = {};
    result.status = body.status;
    result.createdAt = body.createdAt;
    result.startedAt = body.startedAt;
    result.stoppedAt = body.stoppedAt;
    cb(null, result);
  });
};
module.exports.getTask = getTask;