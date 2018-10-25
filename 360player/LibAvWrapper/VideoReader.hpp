/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Based on work by
	Xavier Corbillon
	IMT Atlantique
*/

#pragma once
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <memory>
#include <chrono>
#include <thread>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include <libswscale/swscale.h>
}
#include <GL/glew.h>

#include "Buffer.hpp"
#include "DisplayFrameInfo.hpp"
#include "IOMemoryContext.hpp"
#include "VideoTileStream.hpp"

namespace IMT {
namespace LibAv {

class VideoFrame;
class AudioFrame;

class VideoReader
{
    public:
        VideoReader(VideoTileStream* inputStreams, size_t numInputStreams, size_t bufferSize = 10, float startOffsetInSecond = 102);
        VideoReader(const VideoReader&) = delete;
        VideoReader& operator=(const VideoReader&) = delete;

        virtual ~VideoReader(void);

        void Init(unsigned nbFrames);

        //Update the current binded OpenGL Texture object with the content of the next picture (if right deadline)
        //return the current frame info
        IMT::DisplayFrameInfo SetNextPictureToOpenGLTexture(std::chrono::system_clock::time_point deadline, GLuint textureIds[3]);

        unsigned GetNbStream(void) const {return videoStreamIds.size();}

    protected:

    private:
		VideoTileStream* inputStreams;
		size_t numInputStreams;
		IOMemoryContext** ioCtx;
        AVFormatContext** fmtCtx;
        std::vector<unsigned int> videoStreamIds;
        IMT::Buffer<VideoFrame> outputFrames;
        unsigned nbFrames;
        float startOffsetInSecond;
		double frameDurationMs;
        std::thread decodingThread;
        size_t lastDisplayedPictureNumber;
        size_t videoStreamId;
		std::chrono::system_clock::time_point currentTimestamp;
		std::chrono::duration<double, std::milli> stallingTime;

        void RunDecoderThread(void);
};
}
}
