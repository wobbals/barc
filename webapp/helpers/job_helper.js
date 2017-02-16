var debug = require('debug')('webapp:job_helper');
var validator = require('validator');
var config = require('config');

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
  
  if (args.css_preset && 
    config.get("css_presets").indexOf(args.css_preset) > -1)
  {
    result.css_preset = args.css_preset;
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
  
  return result;
}

module.exports.parseJobArgs = parseJobArgs;