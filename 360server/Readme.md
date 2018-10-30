## Readme

Server for video data delivery.

Build with `g++ main.cpp -pthread -o 360server`

Run with `./360server [pathToWWWDirectory]`

### www directory
The www directory contains files accessible through HTTP requests. 
For our purpose these are MPD files and the DASH video representations.
Our [preprocessing script `tile_and_dash.py`](https://github.com/arnerak/360transitions/tree/master/preprocessing) converts equirectangular videos to the required format.

#### Controlling the server
##### via commands
* `quit` closes server
* `bw [Bytes/s]` sets fixed bandwidth limit
* `trace [pathToNetTrace]` parses [MahiMahi](https://github.com/ravinet/mahimahi) network trace and throttles accordingly

##### via HTTP GET
* `/bw/[Bytes/s]` sets fixed bandwidth limit
* `/trace/[pathToNetTrace]` parses MahiMahi network trace and throttles accordingly
* `/tracereset` starts current MahiMahi trace from beginning