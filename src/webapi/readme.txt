浏览器的webrtc例子。本机交换sdp和candidate

server.js
1. 静态文件
2. pair数组，只能存储两个值0 和 1
   0转发到1， 1转发到0
   每次测试都要重启server


webrtc:
onaddstream 被deprecated
peerVideo.src = URL.createObjectURL(event.stream); 被deprecated
