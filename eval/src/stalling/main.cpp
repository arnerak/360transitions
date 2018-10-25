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

#ifdef _WIN32
typedef std::wstring pathType;
#else
typedef std::string pathType;
#endif

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
		if (traceIndices.find(fi) != traceIndices.end())
			traces.push_back(f.path());
		fi++;
	}

	return traces;
}

void editSquidConf(const std::string& replacement, int cacheSize)
{
	std::ifstream conf("/etc/squid/squid.conf", std::ios::binary);
	std::string line;
	std::vector<std::string> newlines;
	while (std::getline(conf, line))
	{
		if (!line.compare(0, 17, "cache_replacement"))
			newlines.push_back("cache_replacement_policy heap " + replacement);
		else if (!line.compare(0, 14, "cache_dir aufs"))
			newlines.push_back("cache_dir aufs /var/spool/squid " + std::to_string(cacheSize) + " 16 256");
		else if (line[line.size() - 1] == '\r')
			newlines.push_back(line.substr(0, line.size() - 1));
		else
			newlines.push_back(line);
	}
	conf.close();
	std::ofstream confi("/etc/squid/squid.conf", std::ios::trunc);
	for (auto& line : newlines)
		confi << line << "\n";
	confi.close();
}

void resetSquidCache()
{
	system("sudo squid -k shutdown");
	system("sudo service squid stop -k");
	system("sudo rm -rf /var/spool/squid/");
	system("sudo mkdir /var/spool/squid");
	system("sudo chown -R squid:squid /var/spool/squid");
	system("sudo squid -z");
	system("sudo service squid start");
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
}

void downloadPopularTiles()
{
	// download init files
	//for (int i = 0; i < numTiles; i++)
	//	auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());

	double vidDurationMs = mpd->mediaPresentationDuration.count();
	double segDurationS = mpd->segmentDuration();
	int numSegments = std::ceil(vidDurationMs / 1000.0 / segDurationS);
	
	// iterate over temporal segments
	for (int s = 0; s < numSegments; s++)
	{
		au->downloadPopularTiles(s);
	}
}

void initCache(const std::vector<pathType>& traces)
{
	// download init files
	for (int i = 0; i < numTiles; i++)
		auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());

	double vidDurationMs = mpd->mediaPresentationDuration.count();
	double segDurationS = mpd->segmentDuration();
	int numSegments = std::ceil(vidDurationMs / 1000.0 / segDurationS);
	int numQualityLevels = mpd->period.adaptationSets[0].representations.size();

	std::map<int, std::map<int, double>> tileVisibility;
	int i = 0;

	for (auto trace : traces)
	{
		// parse trace file
		HeadTrace headTrace(trace.c_str());

		// iterate over temporal segments
		for (int s = 0; s < numSegments; s++)
		{
			double segStart = segDurationS * s;
			std::map<int, int> segTileVisibility;

			// iterate over a couple of timestamps inside each segment
			for (double ts = segStart; ts < segStart + segDurationS; ts += 0.25)
			{
				auto headRot = headTrace.rotationForTimestamp(ts);
				// compute tile visibility for head rotation at given timestamp
				auto tv = au->computeTileVisibility(headRot);

				for (auto it = tv.begin(); it != tv.end(); it++)
					segTileVisibility[it->first] += it->second;
			}

			for (int t = 0; t < numTiles; t++)
				if (segTileVisibility.find(t) == segTileVisibility.end())
					segTileVisibility[t] = 1;

			// add segment tile visibility from this headtrace to overall visibility
			for (auto it = segTileVisibility.begin(); it != segTileVisibility.end(); it++)
				tileVisibility[s][it->first] += it->second;

			// compute quality levels and request them to trigger caching
			double max = std::max_element(segTileVisibility.begin(), segTileVisibility.end(), [](auto p1, auto p2) { return p1.second < p2.second; })->second;
			for (auto it = segTileVisibility.begin(); it != segTileVisibility.end(); it++)
			{
				it->second = (int)(numQualityLevels - (numQualityLevels * (it->second / max)));
				auto res = httpClient->Get((mpd->getUrl(s, it->first, it->second)).c_str());
				if (!res)
				{
					std::cout << "Alarm! " << mpd->getUrl(s,it->first,it->second) << std::endl;
					char c;
					std::cin >> c;
				}
			}
		}

		std::cout << "\r" << ++i << "/" << traces.size() << std::flush;
	}

	for (int i = 0; i < 5; i++)
		downloadPopularTiles();


	std::cout << std::endl;
}

void downloadTrace(pathType pathToTrace, std::function<void(int, std::vector<int>)> dlfun)
{
	auto srd = mpd->period.adaptationSets[0].srd;
	int numTiles = srd.th * srd.tv;

	auto headTrace = new HeadTrace(pathToTrace.c_str());

	static CircularBuffer<std::pair<long long, Quaternion>> headRotations;

	headRotations.push({ 0, headTrace->rotationForTimestampIt(0)->second });

	auto dlOrder = au->initAdaption(headRotations[0]);
	dlfun(0, dlOrder);
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
		auto downloadOrder = au->startAdaption(headRotations, i);
		dlfun(i, downloadOrder);
		au->stopAdaption();
	}

	delete headTrace;
}

#include <sys/stat.h>

static std::map<int, int> netTrace;
static int netTraceDur;
int computeSegmentDownloadTime(int timestamp, std::map<int, int> tileQuality, int segment, std::vector<int> downloadOrder)
{
	int dlTimeMs = 0;
	int dlBytes = 0;
	for (int i = 0; i < downloadOrder.size(); i++)
	{
		int tileIndex = downloadOrder[i];
		auto url = mpd->getUrl(segment, tileIndex, dlTimeMs * 0.75 > (mpd->segmentDuration() * 1000) ? 2 : tileQuality[tileIndex]);
		if (au->isCached(url))
			continue;

		url = "/home/arak/360server/www" + url;
		
		struct stat statbuf;
		stat(url.c_str(), &statbuf);

		int bytes = statbuf.st_size;
		dlBytes += bytes;
		int ms = 0;
		while (bytes > 0)
		{
			int bw = netTrace[(timestamp + ms)%netTraceDur];
			bytes -= bw;
			ms++;
		}
		dlTimeMs += ms;

	}
	auto bwEstimate = dlBytes * (1000.0 / dlTimeMs);
	au->setBandwidthEstimate(bwEstimate);
	return dlTimeMs;
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
	httpClient->proxyServer = true;

	auto httpClientDirect = new httplib::Client("localhost", 80);
	httpClientDirect->proxyServer = false;

	auto res = httpClientDirect->Get(config->mpdUri.c_str());
	if (!res || res->status != 200)
	{
		std::cout << "MPD not found " << res->status << std::endl;
		return -1;
	}
	mpd = new DASH::MPD(res->body);
	au = new AdaptionUnit(mpd, httpClient, httpClientDirect);

	auto srd = mpd->period.adaptationSets[0].srd;
	numTiles = srd.th * srd.tv;
	int numSegments = mpd->period.adaptationSets[0].representations[0].segmentList.segmentUrls.size();

	auto cacheInitTraces = tracePermutation(15);
	auto evalTraces = tracePermutation(30);



#ifndef _WIN32
	std::ofstream tracesUsed("tracesUsed.txt");
	tracesUsed << "== EVAL ==\n";
	for (auto traceStr : evalTraces)
		tracesUsed << strrchr(traceStr.c_str(), '/') << " ";
	tracesUsed << "\n== CACHE INIT ==\n";
#endif
	
	std::map<int, int> highQuality;
	for (int i = 0; i < numTiles; i++)
		highQuality[i] = 0;

	//const std::string netTraces[3] = { "Verizon-LTE-driving", "Verizon-LTE-short", "TMobile-LTE-driving" };
	const int cacheSizes[4] = { 20, 40, 60, 80 };
	const std::string netTraces[1] = { "mytrace" };
	//const int cacheSizes[1] = { 50 };

	std::ofstream csv("test.csv");
	csv << "Stable State,Network Trace,Type,Cache Size (MB),Stalling Start,Stalling Duration\n";


	std::ifstream file("/home/arak/360server/traces/mytrace.down");	
	std::string line;
	int ts;
	while (!file.eof())
	{
		file >> ts;
		if (netTrace.find(ts) != netTrace.end())
			netTrace[ts] += 1500;
		else
			netTrace[ts] = 1500;
	}
	netTraceDur = ts;
	file.close();

	for (int i = 0; i < 30; i++)
	{
		cacheInitTraces = tracePermutation(15);
		for (auto traceStr : cacheInitTraces)
			tracesUsed << strrchr(traceStr.c_str(), '/') << " ";
		tracesUsed << std::endl;

		for (auto currentCacheSize : cacheSizes)
		for (auto& currentTrace : netTraces)
		{
			httpClient->Get("/bw/99999999");
			editSquidConf("LFUDA", currentCacheSize);
			resetSquidCache();
			initCache(cacheInitTraces);
			

			std::cout << "SS: " << i << " " << currentTrace << " " << currentCacheSize << std::endl;
			//httpClient->Get(("/trace/traces/" + currentTrace + ".down").c_str());

			au->resetCacheHitrateVars();


			
			//Config::instance()->popularity = true;
			//Config::instance()->viewportPrediction = false;
			//Config::instance()->transitions = false;
			//Config::instance()->bwAdaption = false;
			//httpClient->Get("/tracereset");
			auto startTime = TIME_NOW_EPOCH_MS;
			auto playbackTime = 0;
			auto stallTime = 0;
			//downloadTrace(cacheInitTraces[0], [&](int segment)
			//{
			//	std::cout << "\rPopular " << segment + 1 << "/" << numSegments << std::flush;
			//	auto tileQuality = au->getCurrentTileQuality();
			//	playbackTime += computeSegmentDownloadTime(playbackTime, tileQuality, segment);
			//	int stallTimeMs = playbackTime - ((segment + 1) * 1500 + stallTime);
			//	if (stallTimeMs > 0)
			//	{
			//		stallTime += stallTimeMs;
			//		csv << i << "," << currentTrace << ",Popular," << currentCacheSize << "," << playbackTime << "," << stallTimeMs << "\n";
			//	}
			//	else
			//		playbackTime -= stallTimeMs;
			//});
			//csv.flush();			
			//std::cout << "   BHR " << au->byteHitrate() << std::endl;
			//au->resetCacheHitrateVars();



			Config::instance()->popularity = true;
			Config::instance()->viewportPrediction = true;
			Config::instance()->transitions = true;
			Config::instance()->bwAdaption = false;
			//httpClient->Get("/tracereset");
			startTime = TIME_NOW_EPOCH_MS;
			playbackTime = 0;
			stallTime = 0;
			downloadTrace(evalTraces[i], [&](int segment, std::vector<int> downloadOrder)
			{
				std::cout << "\rTransition " << segment + 1 << "/" << numSegments << std::flush;
				auto tileQuality = au->getCurrentTileQuality();
				playbackTime += computeSegmentDownloadTime(playbackTime, tileQuality, segment, downloadOrder);
				int stallTimeMs = playbackTime - ((segment + 1) * 1500 + stallTime);
				if (stallTimeMs > 0)
				{
					stallTime += stallTimeMs;
					csv << i << "," << currentTrace << ",Transition," << currentCacheSize << "," << playbackTime << "," << stallTimeMs << "\n";
				}
				else
					playbackTime -= stallTimeMs;
			});
			csv.flush();
			std::cout << "   BHR " << au->byteHitrate() << std::endl;
			au->resetCacheHitrateVars();



			//Config::instance()->popularity = false;
			//Config::instance()->viewportPrediction = true;
			//Config::instance()->transitions = false;
			//Config::instance()->bwAdaption = false;
			////httpClient->Get("/tracereset");
			//startTime = TIME_NOW_EPOCH_MS;
			//playbackTime = 0;
			//stallTime = 0;
			//downloadTrace(evalTraces[i], [&](int segment)
			//{
			//	std::cout << "\rPrediction " << segment + 1 << "/" << numSegments << std::flush;
			//	auto tileQuality = au->getCurrentTileQuality();
			//	playbackTime += computeSegmentDownloadTime(playbackTime, tileQuality, segment);
			//	int stallTimeMs = playbackTime - ((segment + 1) * 1500 + stallTime);
			//	if (stallTimeMs > 0)
			//	{
			//		stallTime += stallTimeMs;
			//		csv << i << "," << currentTrace << ",Prediction," << currentCacheSize << "," << playbackTime  << "," << stallTimeMs << "\n";
			//	}
			//	else
			//		playbackTime -= stallTimeMs;
			//});
			//csv.flush();
			//std::cout << "   BHR " << au->byteHitrate() << std::endl;
			//au->resetCacheHitrateVars();


			//
			//Config::instance()->popularity = false;
			//Config::instance()->viewportPrediction = true;
			//Config::instance()->transitions = false;
			//Config::instance()->bwAdaption = true;
			////httpClient->Get("/tracereset");
			//startTime = TIME_NOW_EPOCH_MS;
			//playbackTime = 0;
			//stallTime = 0;
			//downloadTrace(evalTraces[i], [&](int segment)
			//{
			//	std::cout << "\rPredictionBWA " << segment + 1 << "/" << numSegments << std::flush;
			//	auto tileQuality = au->getCurrentTileQuality();
			//	playbackTime += computeSegmentDownloadTime(playbackTime, tileQuality, segment);
			//	int stallTimeMs = playbackTime - ((segment + 1) * 1500 + stallTime);
			//	if (stallTimeMs > 0)
			//	{
			//		stallTime += stallTimeMs;
			//		csv << i << "," << currentTrace << ",PredictionBWA," << currentCacheSize << "," << playbackTime << "," << stallTimeMs << "\n";
			//	}
			//	else
			//		playbackTime -= stallTimeMs;
			//});
			//csv.flush();
			//std::cout << "   BHR " << au->byteHitrate() << std::endl;
			//au->resetCacheHitrateVars();




			//Config::instance()->popularity = false;
			//Config::instance()->viewportPrediction = false;
			//Config::instance()->transitions = false;
			//Config::instance()->bwAdaption = false;
			////httpClient->Get("/tracereset");
			//startTime = TIME_NOW_EPOCH_MS;
			//playbackTime = 0;
			//stallTime = 0;
			//downloadTrace(cacheInitTraces[0], [&](int segment)
			//{
			//	std::cout << "\rNaive " << segment + 1 << "/" << numSegments << std::flush;
			//	auto tileQuality = std::map<int,int>();
			//	playbackTime += computeSegmentDownloadTime(playbackTime, tileQuality, segment);
			//	int stallTimeMs = playbackTime - ((segment + 1) * 1500 + stallTime);
			//	if (stallTimeMs > 0)
			//	{
			//		stallTime += stallTimeMs;
			//		csv << i << "," << currentTrace << ",Naive," << currentCacheSize << "," << playbackTime << "," << stallTimeMs << "\n";
			//	}
			//	else
			//		playbackTime -= stallTimeMs;
			//});
			//csv.flush();
			//std::cout << "   BHR " << au->byteHitrate() << std::endl;
			//au->resetCacheHitrateVars();
		}
	}
	csv.close();

	return 0;
}
