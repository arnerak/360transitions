/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Class that handles tile quality selection
	based on bandwidth, viewport and popularity
*/

#pragma once

#include <cmath>
#include <map>
#include <deque>
#include <numeric>
#include <algorithm>

#include "Quaternion.hpp"
#include "mpd.h"
#include "httplib.h"
#include "CircularBuffer.hpp"
#include "ConfigParser.hpp"
#include "Monitor.hpp"

#define TIME_NOW_EPOCH_MS std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
#define SAMPLERES 8
#define SAMPLEPOINTS (SAMPLERES+1)*(SAMPLERES+1)

#define TIMER auto ttt = TIME_NOW_EPOCH_MS
#define TIMEROUT(s) auto ttt2 = TIME_NOW_EPOCH_MS; std::cout << s << " TIMER: " << ttt2 - ttt << std::endl

constexpr float PI = 3.141592653589793238462643383279502884L;

static const double monocular_horizontal = 92.0;
static const double monocular_vertical = 92.0;

static const double maxHDist = 1.5 * std::tan(monocular_horizontal * PI / 180.0 / 2.0);
static const double maxVDist = 1.5 * std::tan(monocular_vertical * PI / 180.0 / 2.0);

using namespace IMT;

class AdaptionUnit
{
public:
	struct NormalizedCoordinate { double x, y; };

	AdaptionUnit(const DASH::MPD* mpd, httplib::Client* httpClient)
		: mpd(mpd), httpClient(httpClient)
		, bytesDownloaded(0), durationDownload(0)
		, bandwidthEstimate(0)
	{
		auto srd = mpd->period.adaptationSets[0].srd;

		int frameWidth = srd.w * srd.th;
		int frameHeight = srd.h * srd.tv;

		for (int i = 0; i < mpd->period.adaptationSets.size(); i++)
		{
			srd = mpd->period.adaptationSets[i].srd;

			double normalizedCoordX = (srd.x + srd.w) / (double)frameWidth;
			double normalizedCoordY = (srd.y + srd.h) / (double)frameHeight;

			normalizedCoordTileMapping[normalizedCoordX][normalizedCoordY] = i;
		}

		#define SIGN(x) (x < 0 ? -1 : 1)
		auto sampleFun = [](int x) { return 0.5 + x * (1.0 / SAMPLERES); };
		//auto sampleFun = [](int x) { return 0.5 + 2 * SIGN(x) * std::pow(x * (1.0 / SAMPLERES), 2); };

		int i = 0;
		for (int x = -SAMPLERES/2; x <= SAMPLERES/2; x++)
			for (int y = -SAMPLERES/2; y <= SAMPLERES/2; y++)
				samplePoints[i++] = { sampleFun(x), sampleFun(y) };

		if (Config::instance()->monitor)
		{
			monitor = new Monitor();
		}
	}

	~AdaptionUnit()
	{
		if (monitor)
			delete monitor;
	}

	void initAdaption(const std::pair<long long, Quaternion>& headRotation)
	{
		CircularBuffer<std::pair<long long, Quaternion>> cb;
		cb.push(headRotation);
		startAdaption(cb, 0, true);
	}

	std::vector<int> startAdaption(const CircularBuffer<std::pair<long long, Quaternion>>& headRotations, int segment, bool init = false)
	{
		std::vector<int> tileDownloadOrder;

		if (init)
			bandwidthEstimate = 2000000;
		else if (durationDownload != 0 && bytesDownloaded != 0)
			bandwidthEstimate = bytesDownloaded * (1000.0 / durationDownload);

		auto timestamp = headRotations[headRotations.size() - 1].first;

		durationDownload = 0;
		bytesDownloaded = 0;

		std::cout << "Start adaption: " << bandwidthEstimate << std::endl;
		
		size_t neededBandwidth = 0;
		int numQualityLevels = mpd->period.adaptationSets[0].representations.size() - 1;
		int numTiles = mpd->period.adaptationSets.size();

		// start with all tiles in lowest quality
		for (int i = 0; i < numTiles; i++)
			tileQuality[i] = numQualityLevels;

		bool transition = false;

		auto config = Config::instance();
		if (config->popularity && !config->viewportPrediction)
		{
			transition = true;
		}
		else if (bandwidthNeededForTileQualityMap(tileQuality) < bandwidthEstimate * .75)
		{
			auto tileVisibility = predictTileVisibility(headRotations);

			auto highestPriorityTile = std::max_element(tileVisibility.begin(), tileVisibility.end(), [](auto p1, auto p2) { return p1.first < p2.first; });
			auto maxVisibility =highestPriorityTile->first;
			auto visibilityPerQualityLevel = int(maxVisibility / (double)numQualityLevels);

			// generate tile download order by visibility
			std::sort(tileVisibility.begin(), tileVisibility.end(), [](auto p1, auto p2) { return p1.first < p2.first; });
			for (std::pair<int, int> tilevis : tileVisibility)
				tileDownloadOrder.push_back(tilevis.second);
			for (int t = 0; t < numTiles; t++)
				if (std::find(tileDownloadOrder.begin(), tileDownloadOrder.end(), t) == tileDownloadOrder.end())
					tileDownloadOrder.push_back(t);

			while (highestPriorityTile->first != 0)
			{
				// enhance quality of highest priority tile
				tileQuality.at(highestPriorityTile->second) = std::max(0, tileQuality.at(highestPriorityTile->second) - 1);

				// trigger transition if too much bandwidth is needed
				if (bandwidthNeededForTileQualityMap(tileQuality) > bandwidthEstimate * .75)
				{
					if (config->popularity && config->transitions)
					{
						transition = true;
						std::cout << "Transition to popularity" << std::endl;
					}
					break;
				}

				// decrease visibility so highest priority tile differs after resort
				highestPriorityTile->first = std::max(0, highestPriorityTile->first - visibilityPerQualityLevel);

				// get most visible tile
				highestPriorityTile = std::max_element(tileVisibility.begin(), tileVisibility.end(), [](auto p1, auto p2) { return p1.first < p2.first; });
			}
		}

		if (transition)
		{
			tileQuality = mpd->tilePopularity(segment);

			// generate tile download order by popularity
			tileDownloadOrder.clear();
			for (int q = 0; q <= numQualityLevels; q++)
				for (int i = 0; i < numTiles; i++)
					if (tileQuality[i] == q)
						tileDownloadOrder.push_back(i);
		}

		monitor->addsample(timestamp / 1000.0, bandwidthEstimate * 8 / 1000000, transition);
		
		downloadStartTime = TIME_NOW_EPOCH_MS;

		return tileDownloadOrder;
		//for (int i = 0; i < 4; i++)
		//{
		//	for (int j = 0; j < 4; j++)
		//		std::cout << tileQuality.at(j * 4 + i) << " ";
		//	std::cout << std::endl;
		//}
	}

	void stopAdaption()
	{
		//if (durationDownload < 200)
		//{
		//	auto timer = TIME_NOW_EPOCH_MS;
		//	auto res = httpClient->Get("/cntrl");
		//	auto duration = TIME_NOW_EPOCH_MS - timer;

		//	bool cacheHit = res->get_header_value("X-Cache").compare(0, 3, "HIT") == 0;
		//	if (!cacheHit)
		//	{
		//		durationDownload += duration;
		//		bytesDownloaded += res->body.size();
		//	}
		//}
	}

	auto download(int tile, int segment = -1)
	{
		if (segment != -1)
			currentSegment = segment;

		bool qOverride = false;
		int lowq = mpd->period.adaptationSets[0].representations.size() - 1;
		if (TIME_NOW_EPOCH_MS - downloadStartTime > 0.75 * (mpd->segmentDuration() * 1000))
		{
			qOverride = true;
			std::cout << "q override "<< TIME_NOW_EPOCH_MS - downloadStartTime << " " << 0.75 * (mpd->segmentDuration() * 1000) << std::endl;
		}
		
		auto timer = TIME_NOW_EPOCH_MS;
		auto res = httpClient->Get(mpd->getUrl(currentSegment, tile, qOverride ? lowq : tileQuality[tile]).c_str());
		auto duration = TIME_NOW_EPOCH_MS - timer;

		bool cacheHit = res->get_header_value("X-Cache").compare(0, 3, "HIT") == 0;
		if (!cacheHit)
		{
			durationDownload += duration;
			bytesDownloaded += res->body.size();
		}

		return res;
	}

	void printTileVisibility(const Quaternion& headRotation)
	{
		std::map<int, int> tileVisibilityMap;
		for (int i = 0; i < SAMPLEPOINTS; i++)
			tileVisibilityMap[mapCoordToTile(fromViewportCoordToEquirectCoord(headRotation, samplePoints[i]))]++;

		for (int i = 0; i < 16; i++)
			if (tileVisibilityMap.find(i) == tileVisibilityMap.end())
				tileVisibilityMap[i] = 0;

		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
				std::cout << tileVisibilityMap.at(j * 4 + i) << "\t";
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}

	const std::map<int, int>& getCurrentTileQuality() const
	{
		return tileQuality;
	}

private:
	const DASH::MPD* mpd;
	httplib::Client* httpClient;
	Monitor* monitor;
	std::map<double, std::map<double, int>> normalizedCoordTileMapping;
	std::map<int, int> tileQuality;
	int currentSegment;	
	size_t bandwidthEstimate;
	size_t bytesDownloaded;
	int durationDownload;
	long long downloadStartTime;
	NormalizedCoordinate samplePoints[SAMPLEPOINTS];
	
	size_t bandwidthNeededForTileQualityMap(const std::map<int, int>& tileQualityMap)
	{
		size_t neededBandwidth = 0;
		for (int i = 0; i < mpd->period.adaptationSets.size(); i++)
			neededBandwidth += mpd->period.adaptationSets[i].representations[tileQualityMap.at(i)].bandwidth;
		return neededBandwidth / 8;
	}

	std::function<double(double)> computeRegressionFunction(const std::vector<double>& x, const std::vector<double>& y) const
	{
		const auto n = x.size();
		const auto s_x = std::accumulate(x.begin(), x.end(), 0.0);
		const auto s_y = std::accumulate(y.begin(), y.end(), 0.0);
		const auto s_xx = std::inner_product(x.begin(), x.end(), x.begin(), 0.0);
		const auto s_xy = std::inner_product(x.begin(), x.end(), y.begin(), 0.0);
		const auto slope = (n * s_xy - s_x * s_y) / (n * s_xx - s_x * s_x);
		const auto intercept = (s_y - slope * s_x) / n;
		return [slope, intercept](double x) { return x * slope + intercept; };
	}

	std::vector<std::pair<int, int>> predictTileVisibility(const CircularBuffer<std::pair<long long, Quaternion>>& headRotations) const
	{
		std::map<int, int> tileVisibilityMap;

		if (headRotations.size() == 1 || !Config::instance()->viewportPrediction)
		{
			// find visible tiles depending on head position
			for (int j = 0; j < SAMPLEPOINTS; j++)
				tileVisibilityMap[mapCoordToTile(fromViewportCoordToEquirectCoord(headRotations[0].second, samplePoints[j]))]++;
		}
		else
		{
			// convert quaternions to euler angles
			std::vector<double> timeVector(headRotations.size());
			std::vector<double> rollVector(headRotations.size());
			std::vector<double> pitchVector(headRotations.size());
			std::vector<double> yawVector(headRotations.size());
			for (int i = 0; i < headRotations.size(); i++)
			{
				auto eulerAngle = headRotations[i].second.ToEuler();
				timeVector[i] = headRotations[i].first;
				rollVector[i] = eulerAngle.GetX();
				pitchVector[i] = eulerAngle.GetY();
				yawVector[i] = eulerAngle.GetZ();
			}

			auto funRegressionRoll = computeRegressionFunction(timeVector, rollVector);
			auto funRegressionPitch = computeRegressionFunction(timeVector, pitchVector);
			auto funRegressionYaw = computeRegressionFunction(timeVector, yawVector);

			auto timestamp = headRotations[0].first;

			static const double segmentDurationMs = mpd->segmentDuration() * 1000;
			const double predictionTimestamps[2] = { timestamp + 0.5 * segmentDurationMs,
				timestamp + segmentDurationMs};

			for (int i = 0; i < 2; i++)
			{
				auto ts = predictionTimestamps[i];
				auto rot = Quaternion::FromEuler(funRegressionYaw(ts), funRegressionPitch(ts), funRegressionRoll(ts));

				// find visible tiles depending on head position
				for (int j = 0; j < SAMPLEPOINTS; j++)
					tileVisibilityMap[mapCoordToTile(fromViewportCoordToEquirectCoord(rot, samplePoints[j]))]++;
			}
		}

		// convert to vector
		std::vector<std::pair<int, int>> tileVisibility;
		for (auto it = tileVisibilityMap.begin(); it != tileVisibilityMap.end(); it++)
			tileVisibility.push_back(std::make_pair(it->second, it->first));

		return tileVisibility;
	}

	int mapCoordToTile(NormalizedCoordinate coord) const
	{
		return normalizedCoordTileMapping.lower_bound(coord.x)->second.lower_bound(coord.y)->second;
	}

	static NormalizedCoordinate fromViewportCoordToEquirectCoord(const Quaternion& headRotation, const NormalizedCoordinate& viewportCoord)
	{
		double u = (viewportCoord.x - 0.5) * (2 * maxHDist);
		double v = (0.5 - viewportCoord.y) * (2 * maxVDist);

		VectorCartesian coordBefRot(1, u, v);
		coordBefRot /= coordBefRot.Norm();

		VectorSpherical pixel3dPolar = headRotation.Rotation(coordBefRot);

		NormalizedCoordinate equirectCoord;
		equirectCoord.x = 1.0 - std::fmod(0.75 + pixel3dPolar.GetTheta() / (2.0*PI), 1.0);
		equirectCoord.y = pixel3dPolar.GetPhi() / PI;
		
		return equirectCoord;
	}
};