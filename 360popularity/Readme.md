## Readme

This application streams a 360° video multiple times according to an arbitrary amount of pre-recorded head traces. 
This initializes the intermediary cache system with popular tile data.
Head traces can be found in /eval/headtraces which were provided by Corbillon et al. in [360-Degree Videos
Head Movements Dataset](http://dash.ipv6.enstb.fr/headMovements/).

Build with `g++ main.cpp tinyxml2.cpp -std=c++14 -lstdc++fs -o 360popularity`

#### Config
```
[Config]
pathHeadtraces=/headtraces/Diving
squidAddress=localhost
squidPort=3128
mpdUri=/dive.mpd
mpdOut=mpdWithPopularityElement.mpd
```