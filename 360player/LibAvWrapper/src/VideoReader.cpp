/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Based on work by
	Xavier Corbillon
	IMT Atlantique
*/

#include "VideoReader.hpp"
#include "Frame.hpp"

//#ifdef USE_OPENGL
#include <GL/glew.h>
#include <GL/gl.h>
#include <SDL2/SDL.h>
//#endif

extern "C"
{
#include "libavutil/opt.h"
}

#include <iostream>
#include <stdexcept>

#define DEBUG_VideoReader 0
#if DEBUG_VideoReader
#define PRINT_DEBUG_VideoReader(s) std::cout <<"DEC -- " << s << std::endl
#else
#define PRINT_DEBUG_VideoReader(s) {}
#endif // DEBUG_VideoReader

extern "C"
{
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

constexpr size_t SDL_AUDIO_BUFFER_SIZE = 1024;

using namespace IMT::LibAv;

VideoReader::VideoReader(VideoTileStream* inputStreams, size_t numInputStreams, size_t bufferSize, float startOffsetInSecond)
	: inputStreams(inputStreams), numInputStreams(numInputStreams), fmtCtx(nullptr), videoStreamIds(), outputFrames(bufferSize)
	, nbFrames(0), startOffsetInSecond(startOffsetInSecond)
	, decodingThread(), lastDisplayedPictureNumber(-1), stallingTime(std::chrono::milliseconds(0))
	, videoStreamId(-1)
{
}

VideoReader::~VideoReader()
{
	SDL_PauseAudio(1);
	if (decodingThread.joinable())
	{
		std::cout << "Join decoding thread\n";
		outputFrames.Stop();
		decodingThread.join();
		std::cout << "Join decoding thread: done\n";
	}
	if (fmtCtx != nullptr)
	{
		for (int i = 0; i < numInputStreams; i++)
		{
			//avcodec_close(m_fmt_ctx[i]->streams[m_videoStreamId]->codec);
			//av_free(m_fmt_ctx[i]->streams[m_videoStreamId]->codec);
			//avformat_close_input(&m_fmt_ctx[i]);
			//avformat_free_context(m_fmt_ctx[i]);

			delete ioCtx[i];
		}
		delete[] ioCtx;

		fmtCtx = nullptr;
	}
}

void printA(AVFormatContext* _a)
{
	std::cout << "duration    " << (unsigned long)_a->duration << "\n";
	std::cout << "streams     " << _a->nb_streams << "\n";
	unsigned nbVideo = 0;
	for (unsigned i = 0; i < _a->nb_streams; ++i)
	{
		if (_a->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			nbVideo++;
		}
	}
	std::cout << "vid stream  " << nbVideo << "\n";
	std::cout << "format name " << _a->iformat->name << "\n";
	std::cout << "bit_rate    " << _a->bit_rate << "\n";
	std::cout << "long name   " << _a->iformat->long_name << "\n";
}

void VideoReader::Init(unsigned nbFrames)
{
	nbFrames = nbFrames;
	PRINT_DEBUG_VideoReader("Register codecs");
	av_register_all();

	int ret = 0;

	ioCtx = new IOMemoryContext*[numInputStreams];

	fmtCtx = new AVFormatContext*[numInputStreams];

	for (int i = 0; i < numInputStreams; i++)
	{
		ioCtx[i] = new IOMemoryContext(inputStreams + i);

		PRINT_DEBUG_VideoReader("Allocate format context");
		if (!(fmtCtx[i] = avformat_alloc_context())) {
			ret = AVERROR(ENOMEM);
			std::cout << "Error while allocating the format context" << std::endl;
		}

		fmtCtx[i]->pb = ioCtx[i]->getAvioContext();

		ret = avformat_open_input(&fmtCtx[i], "", nullptr, nullptr);
		if (ret < 0) {
			std::cout << "Could not open input" << std::endl;
		}

		//if (i > 0)
		//	m_fmt_ctx[i]->iformat = m_fmt_ctx[0]->iformat;
		//else
		//{
		PRINT_DEBUG_VideoReader("Find streams info");
		ret = avformat_find_stream_info(fmtCtx[i], nullptr);
		if (ret < 0) {
			std::cout << "Could not find stream information" << std::endl;
		}
		//}

		//PRINT_DEBUG_VideoReader("Dump format context");
		//av_dump_format(m_fmt_ctx, 0, m_inputPath.c_str(), 0);
		//printA(m_fmt_ctx[i]);

		PRINT_DEBUG_VideoReader("Init video stream decoders");
		if (fmtCtx[i]->nb_streams > 2)
		{
			throw(std::invalid_argument("Support only video with one video stream and one audio stream"));
		}

		AVDictionary *opts_multithread = NULL;
		av_dict_set(&opts_multithread, "threads", "2", 0);

		for (unsigned j = 0; j < fmtCtx[i]->nb_streams; ++j)
		{
			if (fmtCtx[i]->streams[j]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				videoStreamId = j;
				fmtCtx[i]->streams[j]->codec->refcounted_frames = 1;
				//m_outputFrames.emplace_back();
				//m_streamIdToVecId[i] = m_videoStreamIds.size();
				videoStreamIds.push_back(i);
				auto* decoder = avcodec_find_decoder(fmtCtx[i]->streams[j]->codec->codec_id);
				if (!decoder)
				{
					std::cout << "Could not find the decoder for stream id " << j << std::endl;
				}
				PRINT_DEBUG_VideoReader("Init decoder for stream id " << j);
				if ((ret = avcodec_open2(fmtCtx[i]->streams[j]->codec, decoder, &opts_multithread)) < 0)
				{
					std::cout << "Could not open the decoder for stream id " << j << std::endl;
				}
			}
		}
	}

	outputFrames.SetTotal(nbFrames);
	PRINT_DEBUG_VideoReader("Nb frames = " << nbFrames);

	frameDurationMs = 1000.0 / (double(fmtCtx[0]->streams[videoStreamId]->r_frame_rate.num) / fmtCtx[0]->streams[videoStreamId]->r_frame_rate.den);

	PRINT_DEBUG_VideoReader("Start decoding thread");
	decodingThread = std::thread(&VideoReader::RunDecoderThread, this);
}

void VideoReader::RunDecoderThread(void)
{
	AVPacket pkt;
	VideoFrame* tileFrames = new VideoFrame[numInputStreams];

	int ret = -1;
	PRINT_DEBUG_VideoReader("Read next pkt");
	std::cout << "[DEBUG] start offset: " << startOffsetInSecond << std::endl;
	std::chrono::milliseconds m_timeOffset(long(startOffsetInSecond * 1000));
	int64_t seekTimeBasedUnit = startOffsetInSecond * double(fmtCtx[0]->streams[videoStreamId]->time_base.num) / fmtCtx[0]->streams[videoStreamId]->time_base.den;
	seekTimeBasedUnit = 0;

	for (int i = 0; i < numInputStreams; i++)
		av_seek_frame(fmtCtx[i], videoStreamId, seekTimeBasedUnit, 0);

	double frameOffset = 0.0;
	int framenum = 1;

	AVFrame* testFrame = av_frame_alloc();

	while (true)
	{
		for (int i = 0; i < numInputStreams; i++)
		{
			bool hasFrame = false;
			while ((ret = av_read_frame(fmtCtx[i], &pkt)) >= 0)
			{
				unsigned streamId = pkt.stream_index;
				if (streamId == videoStreamId)
				{
					auto* codecCtx = fmtCtx[i]->streams[streamId]->codec;
					ret = avcodec_send_packet(codecCtx, &pkt);

					if (ret == 0)
					{
						ret = tileFrames[i].AvCodecReceiveFrame(codecCtx);
						tileFrames[i].SetFrameOffset(frameOffset);

						if (ret == 0)
						{
							hasFrame = true;
							break;
						}
					}
				}
				av_packet_unref(&pkt);
			}
			if (!hasFrame)
			{
				std::cout << "Decoding thread stopped: video done" << std::endl;
				outputFrames.SetTotal(0);
				delete[] tileFrames;
				return;
			}
		}

		auto frame = std::make_shared<VideoFrame>();
		frame->SetFrameOffset(frameOffset);
		frameOffset += frameDurationMs;
		frame->mergeTilesToFrame(tileFrames, inputStreams, numInputStreams);
		if (!outputFrames.Add(std::move(frame)))
		{
			std::cout << "Decoding thread stopped: frame limit exceeded" << std::endl;
			delete[] tileFrames;
			return;
		}
		else
		{
			PRINT_DEBUG_VideoReader("Added frame to yuvbuffer");
		}
	}
}

IMT::DisplayFrameInfo VideoReader::SetNextPictureToOpenGLTexture(std::chrono::system_clock::time_point deadline, GLuint textureIds[3])
{
	static bool first = true;
	bool last = false;
	auto pts = std::chrono::system_clock::time_point(std::chrono::seconds(-1));
	size_t nbUsed = 0;
	deadline = deadline - std::chrono::duration_cast<std::chrono::system_clock::duration>(stallingTime);
	if (!outputFrames.IsAllDones())
	{
		PRINT_DEBUG_VideoReader("Update video picture");
		std::shared_ptr<VideoFrame> frame(nullptr);
		bool done = false;
		auto tmp_frame = outputFrames.Get();
		auto frameDuration = std::chrono::milliseconds((long)frameDurationMs);
		if (tmp_frame == nullptr && deadline >= currentTimestamp + frameDuration) // Stalling
		{
			stallingTime += deadline - (currentTimestamp + frameDuration);
		}

		while (!done)
		{
			tmp_frame = outputFrames.Get();

			if (tmp_frame != nullptr)
			{
				currentTimestamp = tmp_frame->GetDisplayTimestamp();

				if (deadline >= currentTimestamp)
				{
					PRINT_DEBUG_VideoReader("Updated frame");
					pts = currentTimestamp;
					frame = std::move(tmp_frame);
					outputFrames.Pop();
					++lastDisplayedPictureNumber;
					++nbUsed;
				}
				else
				{
					done = true;
				}
			}
			else
			{
				done = true;
			}
		}

		if (frame != nullptr && frame->IsValid())
		{
			auto w = frame->GetWidth();
			auto h = frame->GetHeight();
				
			if (first)
			{
				for (int i = 0; i < 3; i++)
				{
					glActiveTexture(GL_TEXTURE0 + i);
					glBindTexture(GL_TEXTURE_2D, textureIds[i]);

					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

					glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, i ? w / 2 : w, i ? h / 2 : h, 0, GL_RED, GL_UNSIGNED_BYTE, frame->GetDataPtr()[i]);
				}
				first = false;
			}
			else
			{
				for (int i = 0; i < 3; i++)
				{
					glActiveTexture(GL_TEXTURE0 + i);
					glBindTexture(GL_TEXTURE_2D, textureIds[i]);

					glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, i ? w / 2 : w, i ? h / 2 : h, GL_RED, GL_UNSIGNED_BYTE, frame->GetDataPtr()[i]);
				}
			}
		}
		else if (frame != nullptr && !frame->IsValid())
		{
			PRINT_DEBUG_VideoReader("invalid frame");
			last = true;
			//Stop sound
			SDL_PauseAudio(1);
		}
	}
	else
	{
		last = true;
		//Stop sound
		SDL_PauseAudio(1);
	}
	return { lastDisplayedPictureNumber, nbUsed > 0 ? nbUsed - 1 : 0, deadline, pts, last };
}
