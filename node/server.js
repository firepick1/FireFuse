var fs = require('fs');
var http = require('http');

http.createServer(function(req,resp) {
  resp.writeHead(200, {"Content-Type": "image/jpeg"});

  require("fs");
  var fileName = "/dev/firefuse/cam.jpg";
  fs.readFile(fileName, function(error, data) {
    if (error) throw error;
    resp.write(data);
    resp.end();
  });

  console.log("sample jpg ");
}).listen(8080);
