/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Custom I/O context for ffmpeg
*/

#pragma once

#include "IStream.hpp"

extern "C"
{
#include <libavformat/avio.h>
}

static const int kBufferSize = 32768;

class IOMemoryContext
{
private:
	IOMemoryContext(IOMemoryContext const &);
	IOMemoryContext& operator=(IOMemoryContext const &);

public:
	IOMemoryContext(IStream* inputStream)
		: inputStream(inputStream)
		, bufferSize(kBufferSize)
		, buf((unsigned char*)av_malloc(bufferSize + AV_INPUT_BUFFER_PADDING_SIZE)) 
	{
		avioCtx = avio_alloc_context(buf, bufferSize, 0, this, &IOMemoryContext::read, NULL, &IOMemoryContext::seek);
	}

	~IOMemoryContext() 
	{
		av_free(avioCtx->buffer);
		avio_context_free(&avioCtx);
		//av_free(buf);
	}

	static int read(void* opaque, unsigned char* buf, int buf_size)
	{
		IOMemoryContext* ioCtx = (IOMemoryContext*)opaque;
		auto out = ioCtx->inputStream->read((char*)buf, buf_size);
		return out;
	}

	static int64_t seek(void* opaque, int64_t offset, int whence)
	{
		IOMemoryContext* ioCtx = (IOMemoryContext*)opaque;
		auto out = ioCtx->inputStream->seek(offset, whence);
		return out;
	}

	int read()
	{
		return inputStream->read((char*)buf, kBufferSize);
	}

	int64_t seek(int64_t offset, int whence)
	{
		return inputStream->seek(offset, whence);
	}

	unsigned char* getBufferPtr() const
	{
		return buf;
	}

	AVIOContext* getAvioContext() { return avioCtx; }

private:
	IStream* inputStream;
	int bufferSize;
	unsigned char* buf;
	AVIOContext* avioCtx;
};