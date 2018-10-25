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

const std::string netTraces[3] = { "test_trace", "TMobile-LTE-driving", "Verizon-LTE-driving" };

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

void downloadTrace(pathType pathToTrace, std::function<void(int)> dlfun)
{
	auto srd = mpd->period.adaptationSets[0].srd;
	int numTiles = srd.th * srd.tv;

	auto headTrace = new HeadTrace(pathToTrace.c_str());

	static CircularBuffer<std::pair<long long, Quaternion>> headRotations;

	headRotations.push({ 0, headTrace->rotationForTimestampIt(0)->second });

	int numSegments = mpd->period.adaptationSets[0].representations[0].segmentList.segmentUrls.size();
	double frameRate = mpd->frameRate();
	double segmentDuration = mpd->segmentDuration();
	double segmentFrames = segmentDuration * frameRate;

	au->initAdaption(headRotations[0]);
	dlfun(0);
	au->stopAdaption();

	for (int i = 1; i < numSegments; i++)
	{
		headRotations.clear();
		double startTimestamp = i * segmentDuration - 0.25;
		double endTimestamp = i * segmentDuration;
		auto it = headTrace->rotationForTimestampIt(startTimestamp);
		auto itEnd = std::next(headTrace->rotationForTimestampIt(endTimestamp));
		for (; it != itEnd; it++)
			headRotations.push({ it->first * 1000.0, it->second });
		au->startAdaption(headRotations, i);
		dlfun(i);
		au->stopAdaption();
	}
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
	
	std::ofstream tracesUsed("tracesUsed.txt");
	std::ofstream csv("test.csv");
	csv << "Iteration,NetTrace,Segment,Type,Segment Quality\n";

	httpClient->Get("/bw/99999999");

	Config::instance()->popularity = true;
	Config::instance()->viewportPrediction = false;
	Config::instance()->transitions = false;
	Config::instance()->bwAdaption = false;
	downloadTrace(tracePermutation(1)[0], [&](int segment)
	{
		csv << 0 << ",-," << segment << ",Popularity," << au->getAvgTileQuality() << "\n";
		std::cout << "\r" << segment + 1 << "/" << numSegments << std::flush;
		au->downloadPopularTiles(segment);
	});
	csv.flush();
	std::cout << std::endl;

	for (int i = 0; i < 30; i++)
	{
		std::cout << "It: " << i << "/" << 30 << std::endl;
		auto trace = tracePermutation(1)[0];
		tracesUsed << trace << std::endl;

		httpClient->Get("/bw/99999999");

		Config::instance()->popularity = false;
		Config::instance()->viewportPrediction = true;
		Config::instance()->transitions = false;
		Config::instance()->bwAdaption = false;
		downloadTrace(trace, [&](int segment)
		{
			csv << i << ",-," << segment << ",Prediction," << au->getAvgTileQuality() << "\n";
			std::cout << "\r" << segment + 1 << "/" << numSegments << std::flush;
			for (int i = 0; i < numTiles; i++)
				au->download(i, segment);
		});
		csv.flush();
		std::cout << std::endl;


		for (auto netTrace : netTraces)
		{
			httpClient->Get(("/trace/traces/" + netTrace + ".down").c_str());
			auto startTime = TIME_NOW_EPOCH_MS;
			Config::instance()->popularity = true;
			Config::instance()->viewportPrediction = true;
			Config::instance()->transitions = true;
			Config::instance()->bwAdaption = false;
			downloadTrace(trace, [&](int segment)
			{
				csv << i << "," << netTrace << "," << segment << ",Transition," << au->getAvgTileQuality() << "\n";
				std::cout << "\r" << segment + 1 << "/" << numSegments << std::flush;
				for (int i = 0; i < numTiles; i++)
					au->download(i, segment);
				auto ts = TIME_NOW_EPOCH_MS - startTime;
				int sleepDurMs = (segment + 1) * 1500 - ts;
				if (sleepDurMs > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(sleepDurMs));
			});
			csv.flush();
			std::cout << std::endl;

			Config::instance()->popularity = false;
			Config::instance()->viewportPrediction = true;
			Config::instance()->transitions = false;
			Config::instance()->bwAdaption = true;
			httpClient->Get("/tracereset");
			startTime = TIME_NOW_EPOCH_MS;
			downloadTrace(trace, [&](int segment)
			{
				csv << i << "," << netTrace << "," << segment << ",PredictionBWA," << au->getAvgTileQuality() << "\n";
				std::cout << "\r" << segment + 1 << "/" << numSegments << std::flush;
				for (int i = 0; i < numTiles; i++)
					au->download(i, segment);
				auto ts = TIME_NOW_EPOCH_MS - startTime;
				int sleepDurMs = (segment + 1) * 1500 - ts;
				if (sleepDurMs > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(sleepDurMs));
			});
			csv.flush();
			std::cout << std::endl;
		}			
	}
	

	csv.close();

	return 0;
}
