ECEN602 HW3 Programming Assignment 
(implementation of HTTP1.0 proxy server and client)
-----------------------------------------------------------------

Team Number: 29
Member 1 # Zou, Lihao (UIN: 923009990)
Member 2 # Shalini (UIN: 523005667)
---------------------------------------

Description/Comments:
--------------------
1. Receive the GET request from client and forward to web server, 
get the file from web server and forward to client
2. 10 entry cache table withhold the files in the LRU style
3. Implement conditional GET when no cached file exists but the file pool has got the file, 
when the web server confirm the expire time, transmit local file to client

Notes from your TA:
-------------------
1. While submitting, delete all the files and submit HTTP1.0 proxy server, client files along with makefile and readme.txt to Google Drive folder shared to each team.
2. Don't create new folder for submission. Upload your files to teamxx/HW3.
3. Do not make readme.txt as a google document. Keep it as a text file.
4. I have shared 'temp' folder for your personal (or team) use. You can do compilation or code share among team members using this folder.
5. Make sure to check file transfer between different machines. While grading, I'll use two different machines (lab 213A, Zachry) for your proxy server and client.
6. This is a standard protocol, your client should be compatible with proxy server of other teams. Your proxy server should be compatible with clients of other teams.

All the best. 

Unix command for starting proxy server:
------------------------------------------
./proxy PROXY_SERVER_IP PROXY_SERVER_PORT

Unix command for starting client:
----------------------------------------------
./client PROXY_SERVER_IP PROXY_SERVER_PORT URL

