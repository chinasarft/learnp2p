const express = require('express');
var util = require('util');  

var pair = {}
pair.clients = []

const app = express();
var expressWs = require('express-ws')(app);  
app.use('/static', express.static('public'));

app.get('/', function (req, res) {
   res.send('Hello World!');
})

app.ws('/ws', function(ws, req) { 
  util.inspect(ws);  
  ws.cidx = pair.clients.length
  console.log("one client connect")
  pair.clients.push(ws)
  ws.on('message', function(msg) {  
    console.log("msg:", msg);  
    if(pair.clients.length == 1){
      console.log('xxxx---->:no peer')
      return
    }
    console.log(ws.cidx, ' ---->', (ws.cidx + 1) % 2)
    pair.clients[(ws.cidx + 1) % 2].send(msg)
  });  
})

const server = app.listen(8088, function () {
  const host = server.address().address;
  const port = server.address().port;
  console.log("访问地址为 http://%s:%s", host, port);
})
