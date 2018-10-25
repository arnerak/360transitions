/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Based on work by
	Xavier Corbillon
	IMT Atlantique
*/

#pragma once

extern "C"
{
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include "libavutil/opt.h"
}
#include <chrono>

#include <iostream>
#include <omp.h>

#include "ConfigParser.hpp"

namespace IMT {
namespace LibAv {

class Frame
{
public:
	Frame(void) : m_framePtr(nullptr), m_time_base{ 0,0 }, m_haveFrame(false),
		m_timeOffset(0)
	{
		m_framePtr = av_frame_alloc();
	}

	virtual ~Frame(void)
	{
		if (IsValid())
		{
			av_freep(&m_framePtr->data[0]);
			av_frame_unref(m_framePtr);
			m_haveFrame = -1;
			av_frame_free(&m_framePtr);
			m_framePtr = nullptr;
		}
	}

	bool IsValid(void) const { return m_haveFrame; }
	void SetTimeBase(AVRational time_base) { m_time_base = std::move(time_base); }
	void SetTimeOffset(const std::chrono::milliseconds& timeOffset) { m_timeOffset = timeOffset; }
	auto GetDisplayTimestamp(void) const {	if (IsValid()) return std::chrono::system_clock::time_point(std::chrono::milliseconds(long(frameOffset))); else	return std::chrono::system_clock::time_point(std::chrono::milliseconds(-1)); }
	uint8_t** GetDataPtr(void) const { if (IsValid()) { return m_framePtr->data; } else { return nullptr; } }
	int* GetLinesizePtr(void) const { if (IsValid()) { return m_framePtr->linesize; } else { return nullptr; } }
	size_t GetDisplayPictureNumber(void) const { if (IsValid()) { return m_framePtr->display_picture_number; } else { return -1; } }
	void SetFrameOffset(double offset) { frameOffset = offset; }
protected:
	bool m_haveFrame;
	AVFrame* m_framePtr;
private:
	Frame(Frame const&) = delete;
	Frame& operator=(Frame const&) = delete;

	AVRational m_time_base;
	std::chrono::milliseconds m_timeOffset;
	double frameOffset;
};



class VideoFrame final : public Frame
{
public:
	VideoFrame(void) : Frame() {}
	virtual ~VideoFrame(void) = default;

	auto AvCodecReceiveFrame(AVCodecContext* codecCtx)
	{
		auto ret = avcodec_receive_frame(codecCtx, m_framePtr);
		m_haveFrame = (ret == 0);
		return ret;
	}

	void mergeTilesToFrame(const VideoFrame* tiles, const VideoTileStream* streams, size_t numTiles)
	{
		const DASH::SRD& srd = streams->getSRD();

		int dstWidth = srd.w * srd.th;
		int dstHeight = srd.h * srd.tv;

		av_image_alloc(m_framePtr->data, m_framePtr->linesize, dstWidth, dstHeight, AV_PIX_FMT_YUV420P, 1);

		m_framePtr->width = dstWidth;
		m_framePtr->height = dstHeight;

		if (Config::instance()->demo)
		{
			for (int t = 0; t < numTiles; t++)
			{
				auto timestamp = tiles[t].GetDisplayTimestamp().time_since_epoch().count();
				auto quality = streams[t].getQualityAtTime(timestamp / 1000);

				int srcX = streams[t].getSRD().x;
				int srcY = streams[t].getSRD().y;
				uint8_t* dstPtrBaseY = m_framePtr->data[0] + srcY * m_framePtr->linesize[0] + srcX;
				uint8_t* dstPtrBaseU = m_framePtr->data[1] + srcY / 2 * m_framePtr->linesize[1] + srcX / 2;
				uint8_t* dstPtrBaseV = m_framePtr->data[2] + srcY / 2 * m_framePtr->linesize[2] + srcX / 2;
				uint8_t** srcData = tiles[t].GetDataPtr();
				const int* srcLinesize = tiles[t].GetLinesizePtr();

				for (int l = 0; l < srd.h; l++)
				{
					void* dst = dstPtrBaseY + l * m_framePtr->linesize[0];
					const void* src = srcData[0] + l * srcLinesize[0];
					memset(dst, 127, srd.w); // Y

					if (l % 2)
					{
						int lh = l >> 1;
						int wh = srd.w >> 1;

						dst = dstPtrBaseU + lh * m_framePtr->linesize[1];
						src = srcData[1] + lh * srcLinesize[1];
						memset(dst, 0, wh); // U

						dst = dstPtrBaseV + lh * m_framePtr->linesize[2];
						src = srcData[2] + lh * srcLinesize[2];
						memset(dst, quality * (255 / 3), wh); // V
					}
				}
			}
		}
		else for (int t = 0; t < numTiles; t++)
		{
			int srcX = streams[t].getSRD().x;
			int srcY = streams[t].getSRD().y;
			uint8_t* dstPtrBaseY = m_framePtr->data[0] + srcY * m_framePtr->linesize[0] + srcX;
			uint8_t* dstPtrBaseU = m_framePtr->data[1] + srcY / 2 * m_framePtr->linesize[1] + srcX / 2;
			uint8_t* dstPtrBaseV = m_framePtr->data[2] + srcY / 2 * m_framePtr->linesize[2] + srcX / 2;
			uint8_t** srcData = tiles[t].GetDataPtr();
			const int* srcLinesize = tiles[t].GetLinesizePtr();

			for (int l = 0; l < srd.h; l++)
			{
				void* dst = dstPtrBaseY + l * m_framePtr->linesize[0];
				const void* src = srcData[0] + l * srcLinesize[0];
				memcpy(dst, src, srd.w); // Y

				if (l % 2)
				{
					int lh = l >> 1;
					int wh = srd.w >> 1;

					dst = dstPtrBaseU + lh * m_framePtr->linesize[1];
					src = srcData[1] + lh * srcLinesize[1];
					memcpy(dst, src, wh); // U

					dst = dstPtrBaseV + lh * m_framePtr->linesize[2];
					src = srcData[2] + lh * srcLinesize[2];
					memcpy(dst, src, wh); // V
				}
			}
		}
		m_haveFrame = true;
	}

	int* GetRowLength(void) { if (IsValid()) { return m_framePtr->linesize; } else { return nullptr; } }
	int GetWidth(void) const { if (IsValid()) { return m_framePtr->width; } else { return -1; } }
	int GetHeight(void) const { if (IsValid()) { return m_framePtr->height; } else { return -1; } }
};
}
}
