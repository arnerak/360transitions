/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/

#ifndef VIDEOSEGMENTSTREAM_HPP
#define VIDEOSEGMENTSTREAM_HPP

#include "IStream.hpp"
#include <sstream>
#include "mpd.h"

#define DEBUGVSS 0
#if DEBUGVSS
#define PRINT_DEBUG_VSS(s) std::cout <<"VSS -- " << s << std::endl
#else
#define PRINT_DEBUG_VSS(s) {}
#endif

class VideoTileStream : public IStream
{
public:
	VideoTileStream()
	{
		swappedSize = 0;
		swapReady = false;
		done = false;
	}

	void init(const DASH::SRD& srd, const std::string& init, const std::string& firstSegment)
	{
		this->srd = srd;
		ss1 = stream(init);
		ss1.append(firstSegment);
		activeStream = &ss1;
	}

	const DASH::SRD& getSRD() const
	{
		return srd;
	}

	void addSegment(const std::string& segment, bool last = false)
	{
		std::lock_guard<std::mutex> l(mtx);

		if (activeStream == &ss1)
		{
			PRINT_DEBUG_VSS("append to s2");
			ss2.append(segment);
		}
		else
		{
			PRINT_DEBUG_VSS("append to s1");
			ss1.append(segment);
		}

		swapReady = true;
		done = last;
		cv.notify_all();
	}

	~VideoTileStream()
	{
		
	}

	int read(char* buf, int buf_size) override
	{
		auto ret = activeStream->read(buf, buf_size);
		if (ret == 0 && swap())
			ret = activeStream->read(buf, buf_size);
		//PRINT_DEBUG_VSS("read " << ret);
		return ret;
	}

	int64_t seek(int64_t offset, int whence) override
	{
		offset -= swappedSize;
		
		if (whence == 0x10000)
		{
			return activeStream->size + swappedSize;
		}

		return activeStream->seek(offset, whence);
	}

	bool swap()
	{
		{
			std::unique_lock<std::mutex> lock(mtx);
			cv.wait(lock, [=] { return swapReady || done; });
		}

		if (done && !swapReady)
			return false;

		std::lock_guard<std::mutex> l(mtx);
		swapReady = false;

		if (activeStream == &ss1 && ss2.good())
		{
			PRINT_DEBUG_VSS("swap to s2");
			swappedSize += ss1.size;
			activeStream = &ss2;
			return true;
		}
		if (activeStream == &ss2 && ss1.good())
		{
			PRINT_DEBUG_VSS("swap to s1");
			swappedSize += ss2.size;
			activeStream = &ss1;
			return true;
		}
		return false;
	}

	int getQualityAtTime(double timestamp) const
	{
		return std::prev(qualityLevelAtTimestampMap.upper_bound(timestamp))->second;
	}

	void addQuality(double timestamp, int quality)
	{
		qualityLevelAtTimestampMap[timestamp] = quality;
	}

private:
	struct stream
	{
		std::istringstream s;
		int64_t size;

		stream() : size(0) {}

		stream(const std::string& str)
		{
			init(str);
		}

		int read(char* buf, int buf_size)
		{
			s.read(buf, buf_size);
			return s.gcount();
		}

		int64_t seek(int64_t offset, int whence)
		{
			if (!s.good())
				s.clear();

			s.seekg(offset, whence);
			return s.tellg();
		}

		void append(const std::string& str)
		{
			if (size > 0 && good())
			{
				std::string concat;
				concat.resize(size);
				s.read(&concat[0], size);
				concat += str;
				s = std::istringstream(concat);
				size = concat.size();
			}
			else
				init(str);
		}

		bool good()
		{
			return s.good();
		}

	private:
		void init(const std::string& str)
		{
			PRINT_DEBUG_VSS("Stream::init");

			size = str.size();
			s = std::istringstream(str);
		}
	};

	int64_t swappedSize;
	stream* activeStream;
	stream ss1, ss2;
	mutable std::mutex mtx;
	std::condition_variable cv;
	bool swapReady = false;
	bool done = false;
	DASH::SRD srd;
	std::map<double, int> qualityLevelAtTimestampMap;
};

#endif