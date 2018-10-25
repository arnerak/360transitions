## Readme

This folder contains all evaluation results and the source code of each application used for evaluation.
Each application is started with a start parameter containing the path of a configuration file.
For most evaluation applications both cache and server need to be running.

Should an evaluation be dependent on head traces, then the traces used for our evaluations are included in the respective `txt` document.

Build with `g++ -g main.cpp tinyxml2.cpp -std=c++14 -lstdc++fs -o 360eval`

#### Sample config
Some parameters are programmatically adapted by evaluation applications
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

[Headtrace]
useTrace=True
path=/home/arak/360server/headtraces/Diving
```



### Squid config used for evaluation
Some parameters are programmatically adapted by evaluation applications.
The file should be located in /etc/squid/squid.conf
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