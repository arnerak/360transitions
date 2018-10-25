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
Config* Config::_instance = 0;

//static global variable
static httplib::Client* httpClient;
static DASH::MPD* mpd;
static AdaptionUnit* au;
static HeadTrace* headTrace;
static int numTiles = 0;

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

	double timeframes[4] = { 0.1, 0.25, 0.5, 1.0 };
	double predictiontimes[4] = { 0.5, 1.0, 1.5, 2.0 };
	std::cout << "Prediction Time (s),Timeframe (s),Error (Deg.)" << std::endl;
	for (int l = 0; l < 4; l++)
	{
		double predictionTime = predictiontimes[l];
		for (int k = 0; k < 4; k++)
		{
			double predictionTimeframe = timeframes[k];
			int j = 0;
			// iterate all trace files in folder
			for (auto& f : std::experimental::filesystem::directory_iterator(config->headtracePath))
			{
				++j;
				//if (j == 2)break;
				//if (j < 20) continue;
				//if (j == 21) break;

				// parse trace file
				headTrace = new HeadTrace(f.path().c_str());
				au->resetCacheHitrateVars();

				static CircularBuffer<std::pair<long long, Quaternion>> headRotations;

				headRotations.push({ 0, headTrace->rotationForTimestampIt(0)->second });

				//au->initAdaption(headRotations[0]);
				//for (int i = 0; i < numTiles; i++)
				//{
				//	auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());
				//	auto fsRes = au->download(i, 0);
				//}
				//au->stopAdaption();

				int numSegments = mpd->period.adaptationSets[0].representations[0].segmentList.segmentUrls.size();
				double frameRate = mpd->frameRate();
				double segmentDuration = mpd->segmentDuration();
				double segmentFrames = segmentDuration * frameRate;

				double errorAcc = 0;
				int numPredictions = 0;

				for (int i = 2; i < numSegments; i++)
				{
					int firstSegmentFrame = i * segmentFrames;

					headRotations.clear();
					double startTimestamp = i * segmentDuration - predictionTimeframe;
					double endTimestamp = i * segmentDuration;
					auto it = headTrace->rotationForTimestampIt(startTimestamp);
					auto itEnd = std::next(headTrace->rotationForTimestampIt(endTimestamp));
					for (; it != itEnd; it++)
						headRotations.push({ it->first * 1000.0, it->second });

					auto when = endTimestamp + predictionTime;
					auto predicted = au->predictHeadRotation(headRotations, when * 1000);
					auto actual = headTrace->rotationForTimestampIt(when)->second;
					errorAcc += Quaternion::OrthodromicDistance(actual, predicted);
					numPredictions++;
				}
				if (std::isnormal(errorAcc / numPredictions * (180.0 / PI)))
					std::cout << predictionTime << "," << predictionTimeframe << "," << errorAcc / numPredictions * (180.0 / PI) << std::endl;
				//au->printByteHitrate();
			}
		}
	}

	return 0;
}
