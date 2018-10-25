/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/
#pragma once

#include <chrono>

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#else
#include <pthread.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
typedef int socket_t;
#define INVALID_SOCKET (-1)
#endif

namespace httplib
{
static size_t bandwidth = 2000000;
#define TIME_NOW_EPOCH_MS std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
#define SEND_SEGMENTS_PER_SECOND 10
#define SEND_SEGMENT_DURATION 1000 / SEND_SEGMENTS_PER_SECOND

inline int sendBandwidthLimited(const socket_t& sock, const char* ptr, size_t size)
{
	static auto timeSegmentStart = TIME_NOW_EPOCH_MS;
	static size_t sentDataSinceSegmentStart = 0;
	size_t allowedSendSize = bandwidth / SEND_SEGMENTS_PER_SECOND;

	auto now = TIME_NOW_EPOCH_MS;
	if (now - timeSegmentStart >= SEND_SEGMENT_DURATION)
	{
		timeSegmentStart += SEND_SEGMENT_DURATION;
		auto sizeLeftFromLastSendSegment = bandwidth / SEND_SEGMENTS_PER_SECOND - sentDataSinceSegmentStart;
		allowedSendSize += sizeLeftFromLastSendSegment > 0 ? sizeLeftFromLastSendSegment : 0;
		while (now - timeSegmentStart >= SEND_SEGMENT_DURATION)
		{
			timeSegmentStart += SEND_SEGMENT_DURATION;
			allowedSendSize += bandwidth / SEND_SEGMENTS_PER_SECOND;
		}
		sentDataSinceSegmentStart = 0;
	}

	auto n = 0;
	if (size + sentDataSinceSegmentStart > allowedSendSize)
	{
		const char* cp = ptr;
		while (size + sentDataSinceSegmentStart > allowedSendSize)
		{
			// send remaining allowed data in send segment
			auto sentSize = send(sock, cp, allowedSendSize - sentDataSinceSegmentStart, 0);
			n += sentSize;

			// sleep for rest of the send segment
			now = TIME_NOW_EPOCH_MS;
			auto sleepDuration = SEND_SEGMENT_DURATION - (now - timeSegmentStart);
			if (sleepDuration > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepDuration));

			sentDataSinceSegmentStart = 0;
			timeSegmentStart = timeSegmentStart + SEND_SEGMENT_DURATION;

			size -= sentSize;
			cp += sentSize;

			allowedSendSize = bandwidth / SEND_SEGMENTS_PER_SECOND;
		}

		// send remaining data that fits into one send segment
		sentDataSinceSegmentStart = send(sock, cp, size, 0);
		n += sentDataSinceSegmentStart;
	}
	else
	{
		n = send(sock, ptr, size, 0);
		sentDataSinceSegmentStart += n;
	}

	return n;
}
}
