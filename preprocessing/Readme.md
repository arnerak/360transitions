## Readme

[ffmpeg](https://ffmpeg.zeranoe.com/builds/), [ffprobe](https://ffmpeg.zeranoe.com/builds/) and [MP4Box](https://gpac.wp.imt.fr/downloads/) required in `PATH` or same folder as `tile_and_dash.py`.

#### Configuration inside Python script 
```
vidfile = "dive.mkv"                # input video file name
htiles = 4                          # num of horizontal tiles
vtiles = 4                          # num of vertical tiles
segmentDuration = 1500              # milliseconds
bitrateLevels = [ 1, 0.25, 0.0625 ] # video coded in 3 qualities
startFrom = 40                      # splitting start point in seconds
outLength = 30                      # output video length in seconds
```