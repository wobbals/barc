const request = require('request');
const config = require('config');
const jobHelper = require('./job_helper');
const debug = require('debug')('barc:kennel');
const validator = require('validator');
const Job = require('../model/job');

const postTask = function(taskArgs, cb) {
  let args = jobHelper.parseJobArgs(taskArgs);
  let task_version;
  if (taskArgs.version) {
    task_version = taskArgs.version;
  } else {
    task_version = config.get('default_task_version');
  }
  if (!config.has(`task_versions.${task_version}.task`)) {
    cb({error: `unknown task version ${task_version}`, code: 400});
    return;
  }
  let task_description_id = config.get(`task_versions.${task_version}.task`);
  let task_container_id = config.get(`task_versions.${task_version}.container`);
  let taskBody = {};
  taskBody.task = task_description_id;
  taskBody.container = task_container_id;
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

  debug(`task: ${JSON.stringify(taskBody)}`);

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

const getTask = async function(taskId, cb) {
  if (!validator.isUUID(taskId)) {
    cb({error: `invalid taskId ${taskId}`});
    return;
  }
  let job;
  try {
    job = await Job.getJob(taskId);
  } catch (e) {
    debug(e);
  }
  if (!job) {
    cb({error: `unknown job ${taskId}`});
    return;
  }
  debug(job);
  request.get({
    url: `${config.get('kennel_base_url')}/task/${taskId}`
  }, (error, response, bodyStr) => {
    let body = JSON.parse(bodyStr);
    debug(`kennel get task body: ${JSON.stringify(body)}`);
    if (error) {
      debug(`kennel get task error: ${error}`);
      cb({error: 'internal error: kennel query failed'});
      return;
    }
    let result = {};
    result.status = job.status;
    result.progress = job.progress;
    result.clusterStatus = body.status;
    result.createdAt = body.createdAt;
    result.startedAt = body.startedAt;
    result.stoppedAt = body.stoppedAt;
    cb(null, result);
  });
};
module.exports.getTask = getTask;