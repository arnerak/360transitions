/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/

// Internal Includes
#define BOOST_TYPEOF_EMULATION
// Library/third-party includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Standard includes
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <chrono>
#include <stdlib.h> // For exit()
#include <experimental/filesystem>

#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

//Internal Includes
#include "ConfigParser.hpp"
#include "Quaternion.hpp"
#include "mpd.h"
#include "AdaptionUnit.hpp"
#include "HeadTrace.hpp"

using namespace IMT;
namespace fs = std::experimental::filesystem;

Config* Config::_instance = 0;

httplib::Client* httpClient;
DASH::MPD* mpd;
int numTiles;
AdaptionUnit* au;

const std::string netTrace = "Verizon-LTE-driving";

typedef std::string pathType;

std::vector<pathType> tracePermutation(int numTraces)
{
	std::vector<pathType> traces;

	auto dirIt = fs::directory_iterator(Config::instance()->headtracePath);
	auto dirItBegin = fs::begin(dirIt);
	auto dirItEnd = fs::end(dirIt);
	auto numFiles = std::distance(dirItBegin, dirItEnd);

	std::set<int> traceIndices;
	for (int i = 0; i < numTraces; i++)
		while (!traceIndices.insert(std::rand() % numFiles).second);

	int fi = 0;
	for (auto& f : fs::directory_iterator(Config::instance()->headtracePath))
	{
		std::string p;
#ifdef _WIN32
		using convert_type = std::codecvt_utf8<wchar_t>;
		std::wstring_convert<convert_type, wchar_t> converter;
		p = converter.to_bytes(f.path());
#else
		p = f.path();
#endif

		if (traceIndices.find(fi) != traceIndices.end())
			traces.push_back(p);
		fi++;
	}

	return traces;
}

void downloadPopularTiles()
{
	// download init files
	for (int i = 0; i < numTiles; i++)
		auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());

	double vidDurationMs = mpd->mediaPresentationDuration.count();
	double segDurationS = mpd->segmentDuration();
	int numSegments = std::ceil(vidDurationMs / 1000.0 / segDurationS);
	
	// iterate over temporal segments
	for (int s = 0; s < numSegments; s++)
	{
		au->downloadPopularTiles(s);
	}
}

void downloadTrace(pathType pathToTrace, std::function<void(int, bool)> dlfun)
{
	auto srd = mpd->period.adaptationSets[0].srd;
	int numTiles = srd.th * srd.tv;

	auto headTrace = new HeadTrace(pathToTrace.c_str());

	static CircularBuffer<std::pair<long long, Quaternion>> headRotations;

	headRotations.push({ 0, headTrace->rotationForTimestampIt(0)->second });

	bool transition = au->initAdaption(headRotations[0]);
	dlfun(0, transition);
	au->stopAdaption();

	int numSegments = mpd->period.adaptationSets[0].representations[0].segmentList.segmentUrls.size();
	double frameRate = mpd->frameRate();
	double segmentDuration = mpd->segmentDuration();
	double segmentFrames = segmentDuration * frameRate;

	for (int i = 1; i < numSegments; i++)
	{
		headRotations.clear();
		double startTimestamp = i * segmentDuration - 0.25;
		double endTimestamp = i * segmentDuration;
		auto it = headTrace->rotationForTimestampIt(startTimestamp);
		auto itEnd = std::next(headTrace->rotationForTimestampIt(endTimestamp));
		for (; it != itEnd; it++)
			headRotations.push({ it->first * 1000.0, it->second });
		bool transition = au->startAdaption(headRotations, i);
		dlfun(i, transition);
		au->stopAdaption();
	}
}

std::vector<std::string> split(const std::string& s, char seperator)
{
	std::vector<std::string> output;
	std::string::size_type prev_pos = 0, pos = 0;
	while ((pos = s.find(seperator, pos)) != std::string::npos)
	{
		std::string substring(s.substr(prev_pos, pos - prev_pos));
		output.push_back(substring);
		prev_pos = ++pos;
	}
	output.push_back(s.substr(prev_pos, pos - prev_pos));
	return output;
}

int main(int argc, char* argv[])
{
	// Parse the command line
	if (argc != 2)
	{
		std::cout << "Usage: " << argv[0] << " pathToConfig" << std::endl;
		return -1;
	}

	auto config = Config::instance();
	config->init(argv[1]);

	httpClient = new httplib::Client(config->squidAddress.c_str(), config->squidPort);
	//httpClient->proxyServer = true;

	auto res = httpClient->Get(config->mpdUri.c_str());
	if (!res || res->status != 200)
	{
		std::cout << "MPD not found " << res->status << std::endl;
		return -1;
	}
	mpd = new DASH::MPD(res->body);
	au = new AdaptionUnit(mpd, httpClient);

	auto srd = mpd->period.adaptationSets[0].srd;
	numTiles = srd.th * srd.tv;
	int numSegments = mpd->period.adaptationSets[0].representations[0].segmentList.segmentUrls.size();

	
	std::map<int, int> highQuality;
	for (int i = 0; i < numTiles; i++)
		highQuality[i] = 0;

	std::cout << "Setting nettrace..." << std::endl;
	httpClient->Get(("/trace/traces/" + netTrace + ".down").c_str());
	std::cout << "Trace set." << std::endl;

	std::ofstream tracesUsed("tracesUsed.txt");
	std::ofstream csv(netTrace + ".csv");
	csv << "Iteration,Time,Transition\n";

	Config::instance()->popularity = true;
	Config::instance()->viewportPrediction = true;
	Config::instance()->transitions = true;
	for (int i = 0; i < 30; i++)
	{
		std::cout << "It: " << i << "/" << 30 << std::endl;
		auto trace = tracePermutation(1)[0];
		tracesUsed << split(trace, '/').back() << " ";
		tracesUsed << "\n";

		httpClient->Get("/tracereset");
		auto startTime = TIME_NOW_EPOCH_MS;
		downloadTrace(trace, [&](int segment, bool transition)
		{
			csv << i << "," << TIME_NOW_EPOCH_MS - startTime << "," << transition << "\n";
			std::cout << "\r" << segment + 1 << "/" << numSegments << std::flush;
			for (int i = 0; i < numTiles; i++)
				au->download(i, segment);
			auto ts = TIME_NOW_EPOCH_MS - startTime;
			int sleepDurMs = (segment + 1) * 1500 - ts;
			if (sleepDurMs > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepDurMs));
			au->resetTotalBytesDownloaded();
		});
		csv.flush();
		std::cout << std::endl;
	}

	csv.close();

	return 0;
}
