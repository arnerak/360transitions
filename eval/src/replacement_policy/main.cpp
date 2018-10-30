/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/
#include "mpd.h"
#include "httplib.h"
#include "HeadTrace.hpp"
#include "AdaptionUnit.hpp"
#include <experimental/filesystem>
#include "IniReader.hpp"
#include <random>
#include <iostream>
#ifndef _WIN32
#include <libgen.h>
#endif

namespace fs = std::experimental::filesystem;

std::string pathHeadtraces;
httplib::Client* httpClient;
DASH::MPD* mpd;
int numTiles;
AdaptionUnit* au;

#ifdef _WIN32
typedef std::wstring pathType;
#else
typedef std::string pathType;
#endif

void resetSquidCache()
{
	system("sudo squid -k shutdown");
	system("sudo service squid stop -k");
	system("sudo rm -rf /var/spool/squid/");
	system("sudo mkdir /var/spool/squid");
	system("sudo chown -R squid:squid /var/spool/squid");
	system("sudo squid -z");
	system("sudo service squid start");
}

std::vector<pathType> tracePermutation(int numTraces)
{
	std::vector<pathType> traces;

	auto dirIt = fs::directory_iterator(pathHeadtraces);
	auto dirItBegin = fs::begin(dirIt);
	auto dirItEnd = fs::end(dirIt);
	auto numFiles = std::distance(dirItBegin, dirItEnd);
	
	std::set<int> traceIndices;
	for (int i = 0; i < numTraces; i++)
		while (!traceIndices.insert(std::rand()%numFiles).second);
	
	int fi = 0;
	for (auto& f : fs::directory_iterator(pathHeadtraces))
	{
		if (traceIndices.find(fi) != traceIndices.end())
			traces.push_back(f.path());
		fi++;
	}
	
	return traces;
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
				httpClient->Get((mpd->getUrl(s, it->first, it->second)).c_str());
			}
		}

		std::cout << "\r" << ++i << "/" << traces.size() << std::flush;
	}

	std::cout << std::endl;
}

void downloadPopularTiles()
{
	// download init files
	for (int i = 0; i < numTiles; i++)
		auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());

	double vidDurationMs = mpd->mediaPresentationDuration.count();
	double segDurationS = mpd->segmentDuration();
	int numSegments = std::ceil(vidDurationMs / 1000.0 / segDurationS);

	au->resetCacheHitrateVars();

	// iterate over temporal segments
	for (int s = 0; s < numSegments; s++)
	{
		au->downloadPopularTiles(s);
	}
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
		else if (line[line.size()-1] == '\r')
			newlines.push_back(line.substr(0, line.size()-1));
		else
			newlines.push_back(line);
	}
	conf.close();
	std::ofstream confi("/etc/squid/squid.conf", std::ios::trunc);
	for (auto& line : newlines)
		confi << line << "\n";
	confi.close();
}

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		std::cout << "Usage: " << argv[0] << " pathToConfig" << std::endl;
		return 0;
	}
	
	// parse config
	INIReader ini(argv[1]);
	pathHeadtraces = ini.Get("Config", "pathHeadtraces", "");
	std::string mpdUri = ini.Get("Config", "mpdUri", "");
	std::string mpdOut = ini.Get("Config", "mpdOut", "");
	std::string squidAddress = ini.Get("Config", "squidAddress", "");
	int squidPort = ini.GetInteger("Config", "squidPort", 3128);

	httpClient = new httplib::Client(squidAddress.c_str(), squidPort);
	httpClient->proxyServer = true;

	auto res = httpClient->Get(mpdUri.c_str());
	if (!res || res->status != 200)
	{
		std::cout << "MPD not found " << res->status << std::endl;
		return -1;
	}
	mpd = new DASH::MPD(res->body);
	auto srd = mpd->period.adaptationSets[0].srd;
	numTiles = srd.th * srd.tv;
	au = new AdaptionUnit(mpd, httpClient);
	
	const int numStableStates = 30;
	const int numTracesPerInit = 30;
	const char* rps[3] = { "LRU", "LFUDA", "GDSF" };
	const int cacheSizes[4] = { 20, 40, 60, 80 };	

	std::ofstream csv("test.csv");
	std::ofstream tracesUsed("tracesUsed.txt");
	csv << "Replacement Policy,Cache Size (MB),Stable State,BHR,CHR\n";
	for (int i = 0; i < numStableStates; i++)
	{
		auto traces = tracePermutation(numTracesPerInit);
		for (auto traceStr : traces)
			tracesUsed << strrchr(traceStr.c_str(), '/') << " ";
		tracesUsed << "\n";

		for (int r = 0; r < 3; r++)
		{
			for (int c = 0; c < 4; c++)
			{
				std::string replacementPolicy = rps[r];
				int cacheSize = cacheSizes[c];
				std::cout << "Stable State: " << i << "; " << replacementPolicy << "; " << cacheSize << std::endl;
				editSquidConf(replacementPolicy, cacheSize);
				resetSquidCache();
				initCache(traces);
				downloadPopularTiles();
				csv << replacementPolicy << "," << cacheSize << "," << i << "," << 
					std::to_string(au->byteHitrate()) + "," + std::to_string(au->cacheHitrate()) << "\n";
			}
		}
	}
	csv.close();
	tracesUsed.close();
}
