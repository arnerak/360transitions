## Readme

This project is based on [360Degree_Head_Movement_Dataset](https://github.com/xmar/360Degree_Head_Movement_Dataset).

It is complemented by  
* Decoding of DASH-segmented and tiled videos
* Fetching video data from HTTP server
* Quality adaptation based on measured bandwidth, viewer head orientation and tile popularity

### Building
It is recommended to build this application on a Windows x64 system, in order to support _DirectMode_ rendering.
#### Windows (Visual Studio)
##### Requirements
* [OSVR SDK](http://access.osvr.com/binary/osvr-sdk-installer)
* [Boost 1.64.0 Libraries](https://sourceforge.net/projects/boost/files/boost-binaries/1.64.0/)
* [SDL2 Libraries](https://www.libsdl.org/download-2.0.php)
* [SDL2 TTF](https://www.libsdl.org/projects/SDL_ttf/)
* [Glew Library](https://sourceforge.net/projects/glew/files/latest/download)
* [ffmpeg Shared and Dev](https://ffmpeg.zeranoe.com/builds/)
* [Eigen](https://github.com/eigenteam/eigen-git-mirror)

##### Visual Studio
Link shared libraries
```
avcodec.lib
avformat.lib
avutil.lib
glew32.lib
opengl32.lib
osvrClient.lib
osvrClientKit.lib
osvrCommon.lib
osvrRenderManager.lib
osvrUtil.lib
SDL2.lib
SDL2_ttf.lib
swresample.lib
swscale.lib
ws2_32.lib
```
Disable SDL-Checks
Define ```WIN32_LEAN_AND_MEAN``` preprocessor
#### Linux
Please follow [these](https://docs.google.com/document/d/18lGSDgB4gElmcdL4-vVISrxkQCkuwLBbEJFs67u13rg/edit) and [these](https://docs.google.com/document/d/1VSKkVNOF3YH_p7FXpOTS9H_t-x1rXlBHAwMpXhppF9I/edit#heading=h.q82xye1d2ypg) guidelines.

### Running
Start with ```./360player [pathToConfig]``` or ```./360player.exe [pathToConfig]```

