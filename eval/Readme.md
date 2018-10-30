## Readme

This folder contains all evaluation results and the source code of each application used for evaluation.
Each application is started with a start parameter containing the path of a configuration file.

Should an evaluation be dependent on head traces, then the traces used for our evaluations are included in the respective `txt` document.

### Building
`cd` to any eval src folder and build with: 
`g++ main.cpp ../include/tinyxml2.cpp -I../include -std=c++14 -lstdc++fs -o 360eval`


### Running
Run with
`./360eval [pathToConfig]`

For most evaluation applications **both cache and server** need to be running.
For running the server, a www directory must be provided that contains tiled and DASH-ed versions of the videos `2OzlksZBTiA.mkv` (dive), `CIw8R8thnm8.mkv` (nyc) and `8lsB-P8nGSM.mkv` (rollercoaster), which can be downloaded [here](http://dash.ipv6.enstb.fr/headMovements/).
Our [preprocessing script `tile_and_dash.py`](https://github.com/arnerak/360transitions/tree/master/preprocessing) converts equirectangular videos to the required format.
See below for instructions on running a squid cache instance.

#### Sample config
```
[Config]
playConfig=DashConfig

[DashConfig]
type=dash
squidAddress=localhost
squidPort=3128
mpdUri=/dive.mpd
viewportPrediction=True
popularity=True
transitions=True
demo=True
bwAdaption=False

[Headtrace]
useTrace=True
path=/home/arak/360server/headtraces/Diving
```


### Setting up squid cache
Install:
`sudo apt-get install squid`

Edit /etc/squid/squid.conf:
```
http_port 3128
coredump_dir /var/spool/squid
acl SSL_ports port 443
acl Safe_ports port 80          # http
acl Safe_ports port 21          # ftp
acl Safe_ports port 443         # https
acl Safe_ports port 1025-65535  # unregistered ports
acl CONNECT method CONNECT
acl any_host src all
acl all_dst dst all
http_access allow any_host
http_access allow all_dst
http_access deny all

# caching settings
cache_mem 10 MB
cache_replacement_policy heap LFUDA
cache_dir aufs /var/spool/squid 80 16 256
maximum_object_size 10 MB
cache_store_log /var/log/squid/store.log
refresh_pattern -i \.mpd$ 120 90% 999999
refresh_pattern -i \.(m4s|mp4)$ 1440 90% 999999
cache_effective_user squid
```

Add squid user:
```
sudo adduser squid
sudo chown -R squid:squid /etc/squid
sudo chown -R squid:squid /var/log/squid
```

Restart and clear cache:
```
sudo squid -k shutdown
sudo service squid stop -k
sudo rm -rf /var/spool/squid
sudo mkdir /var/spool/squid
sudo chown -R squid:squid /var/spool/squid
sudo squid -z
sudo service squid start
```

