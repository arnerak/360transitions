## Readme

Script for tiling equirectangular videos, encoding them with arbitrary bitrates and generating a DASH representation.

[ffmpeg](https://ffmpeg.zeranoe.com/builds/), [ffprobe](https://ffmpeg.zeranoe.com/builds/) and [MP4Box](https://gpac.wp.imt.fr/downloads/) executables required in `PATH` environment variable or same folder as `tile_and_dash.py`.

#### Configuration inside Python script 
```
vidfile = "dive.mkv"                # input video file name
htiles = 4                          # num of horizontal tiles
vtiles = 4                          # num of vertical tiles
segmentDuration = 1500              # milliseconds
bitrateLevels = [ 1, 0.25, 0.0625 ] # video coded in 3 qualities (# times original bitrate)
startFrom = 40                      # splitting start point in seconds
outLength = 30                      # output video length in seconds
```