## Readme

Server for video data delivery.

Build with `g++ main.cpp -pthread -o 360server`
Run with `./360server [pathToWWWDirectory]`

#### Controlling the server
##### via commands
* `quit` closes server
* `bw [Bytes/s]` sets fixed bandwidth limit
* `trace [pathToNetTrace]` parses [MahiMahi](https://github.com/ravinet/mahimahi) network trace and throttles accordingly

##### via HTTP GET
* `/bw/[Bytes/s]` sets fixed bandwidth limit
* `/trace/[pathToNetTrace]` parses MahiMahi network trace and throttles accordingly
* `/tracereset` starts current MahiMahi trace from beginning