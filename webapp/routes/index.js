var express = require('express');
var router = express.Router();
const spawn = require('child_process').spawn;

router.get('/', function(req, res, next) {
  res.render('index', { title: 'Express' });
});

router.post('/job', function(req, res) {
  var barc = "/Users/charley/Library/Developer/Xcode/DerivedData/barc-eyadjotdfehvblfkxkypuwvhhlut/Build/Products/Debug/barc"
  var cwd = "/Users/charley/Library/Developer/Xcode/DerivedData/barc-eyadjotdfehvblfkxkypuwvhhlut/Build/Products/Debug"
  const child = spawn(barc, ['-w 640', '-h 480'], {
    detached: true,
    stdio: 'ignore',
    cwd: cwd
  });
  console.log("I booted a thing!");

  child.on('close', (code, signal) => {
    console.log(`child process exited with code ${code}`);
    console.log(`child process terminated due to receipt of signal ${signal}`);
  });

  res.json({ pid: child.pid });
});

router.get('/job/:id', function(req, res) {
  
});

module.exports = router;
