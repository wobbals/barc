const debug = require('debug')('webapp:model:job');
const config = require('config');
const redis = require('redis').createClient({
  host: config.get('redis_host'),
  port: config.get('redis_port')
});
const hash_generator = require('random-hash-generator');

const persist = function(taskId, data) {
  debug(`store job ${taskId}: ${JSON.stringify(data)}`);
  return new Promise((resolve, reject) => {
    redis.HMSET(`barc:job:${taskId}`, data, (err, obj) => {
      if (err) {
        reject(err);
      } else {
        resolve(obj);
      }
    });
  });
};
module.exports.persist = persist;

const getJob = function(taskId) {
  return new Promise((resolve, reject) => {
    redis.HGETALL(`barc:job:${taskId}`, (err, obj) => {
      if (err) {
        reject(err);
      } else {
        resolve(obj);
      }
    });
  });
}
module.exports.getJob = getJob;

module.exports.checkKey = async function(taskId, token) {
  let calculated_secret = hash_generator.calc(token,
    config.get("secret_token_length"),
    config.get("secret_token_salt")
  );
  let job;
  try {
    job = await getJob(taskId);
  } catch (e) {
    debug(`checkKey: error getting job:`, e);
    return false;
  }
  return (job.secret === calculated_secret);
}
