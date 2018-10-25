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

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		std::cout << "Usage: " << argv[0] << " pathToConfig" << std::endl;
		return 0;
	}

	// parse config
	INIReader ini(argv[1]);
	std::string pathHeadtraces = ini.Get("Config", "pathHeadtraces", "");
	std::string mpdUri = ini.Get("Config", "mpdUri", "");
	std::string mpdOut = ini.Get("Config", "mpdOut", "");
	std::string squidAddress = ini.Get("Config", "squidAddress", "");
	int squidPort = ini.GetInteger("Config", "squidPort", 3128);

	auto httpClient = new httplib::Client(squidAddress.c_str(), squidPort);
	httpClient->proxyServer = true;

	auto res = httpClient->Get(mpdUri.c_str());
	if (!res || res->status != 200)
	{
		std::cout << "MPD not found " << res->status << std::endl;
		return -1;
	}
	auto mpd = new DASH::MPD(res->body);
	auto srd = mpd->period.adaptationSets[0].srd;
	auto numTiles = srd.th * srd.tv;
	AdaptionUnit au(mpd);

	// download init files
	for (int i = 0; i < numTiles; i++)
		auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());

	double vidDurationMs = mpd->mediaPresentationDuration.count();
	double segDurationS = mpd->segmentDuration();
	int numSegments = vidDurationMs / 1000.0 / segDurationS;
	int numQualityLevels = mpd->period.adaptationSets[0].representations.size();

	std::map<int, std::map<int, double>> tileVisibility;
	int i = 0;

	// iterate all trace files in folder
	for (auto& f : std::experimental::filesystem::directory_iterator(pathHeadtraces))
	{
		// parse trace file
		HeadTrace headTrace(f.path().c_str());

		// iterate over temporal segments
#pragma omp parallel for
		for (int s = 0; s < numSegments; s++)
		{
			double segStart = segDurationS * s;
			std::map<int, int> segTileVisibility;

			// iterate over a couple of timestamps inside each segment
			for (double ts = segStart; ts < segStart + segDurationS; ts += 0.25)
			{
				auto headRot = headTrace.rotationForTimestamp(ts);
				// compute tile visibility for head rotation at given timestamp
				auto tv = au.computeTileVisibility(headRot);

				for (auto it = tv.begin(); it != tv.end(); it++)
					segTileVisibility[it->first] += it->second;
			}

			// add segment tile visibility from this headtrace to overall visibility
			for (auto it = segTileVisibility.begin(); it != segTileVisibility.end(); it++)
#pragma omp atomic
				tileVisibility[s][it->first] += it->second;

			// compute quality levels and request them to trigger caching
			double max = std::max_element(segTileVisibility.begin(), segTileVisibility.end(), [](auto p1, auto p2) { return p1.second < p2.second; })->second;
			for (auto it = segTileVisibility.begin(); it != segTileVisibility.end(); it++)
			{
				it->second = (int)(numQualityLevels - (numQualityLevels * (it->second / max)));
				auto initRes = httpClient->Get((mpd->getUrl(s, it->first, it->second)).c_str());
			}
		}

		std::cout << "\r" << ++i << std::flush;
	}
	std::cout << std::endl;


	// add popularity statistics to mpd file
	XMLDocument& xml = mpd->getXML();
	auto period = xml.FirstChildElement()->FirstChildElement("Period");
	if (period->FirstChildElement("Popularity") == NULL)
	{
		auto popularity = xml.NewElement("Popularity");
		i = 0;
		for (auto it = tileVisibility.begin(); it != tileVisibility.end(); it++)
		{
			auto tp = xml.NewElement("SegmentPopularity");
			tp->SetAttribute("segment", ++i);
			std::string pops;
			double max = std::max_element(it->second.begin(), it->second.end(), [](auto p1, auto p2) { return p1.second < p2.second; })->second;
			for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++)
			{
				it2->second = (int)(numQualityLevels - (numQualityLevels * (it2->second / max)));
				pops += std::to_string((int)it2->second);
				if (it2 != std::prev(it->second.end()))
					pops += ",";
			}
			tp->SetAttribute("tileQuality", pops.c_str());
			popularity->InsertEndChild(tp);
		}
		period->InsertFirstChild(popularity);
		xml.SaveFile(mpdOut.c_str());
	}
}
