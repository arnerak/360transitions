/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/

#pragma once

#include <cmath>
#include <map>
#include <deque>
#include <numeric>
#include <algorithm>
#include <string>
#include <map>

#include "Quaternion.hpp"
#include "mpd.h"
#include "httplib.h"

#define TIME_NOW_EPOCH_MS std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
#define SAMPLERES 8
#define SAMPLEPOINTS (SAMPLERES+1)*(SAMPLERES+1)

#define TIMER auto ttt = TIME_NOW_EPOCH_MS
#define TIMEROUT(s) auto ttt2 = TIME_NOW_EPOCH_MS; std::cout << s << " TIMER: " << ttt2 - ttt << std::endl

 float PI = 3.141592653589793238462643383279502884L;

static const double monocular_horizontal = 92.0;
static const double monocular_vertical = 92.0;

static const double maxHDist = 2 * std::tan(monocular_horizontal * PI / 180.0 / 2.0);
static const double maxVDist = 2 * std::tan(monocular_vertical * PI / 180.0 / 2.0);

using namespace IMT;

class AdaptionUnit
{
public:
	struct NormalizedCoordinate { double x, y; };

	AdaptionUnit(const DASH::MPD* mpd, httplib::Client* httpClient)
		: mpd(mpd), httpClient(httpClient)
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

		for (int i = 0; i <= SAMPLERES; i++)
			for (int j = 0; j <= SAMPLERES; j++)
				samplePoints[i * (SAMPLERES + 1) + j] = { i * (1.0 / SAMPLERES), j * (1.0 / SAMPLERES) };
	}


	void initAdaption(const std::pair<long long, Quaternion>& headRotation)
	{
		CircularBuffer<std::pair<long long, Quaternion>> cb;
		cb.push(headRotation);
		startAdaption(cb, 0, true);
	}

	void startAdaption(const CircularBuffer<std::pair<long long, Quaternion>>& headRotations, int segment, bool init = false)
	{
		size_t bandwidthEstimate = 4200000;
		

		//std::cout << "Start adaption: " << bandwidthEstimate << std::endl;
		
		size_t neededBandwidth = 0;
		int numQualityLevels = mpd->period.adaptationSets[0].representations.size() - 1;
		int numTiles = mpd->period.adaptationSets.size();

		// start with all tiles in lowest quality
		for (int i = 0; i < numTiles; i++)
			tileQuality[i] = numQualityLevels;

		bool transition = false;

	
		
		if (bandwidthNeededForTileQualityMap(tileQuality) < bandwidthEstimate * .75)
		{
			auto tileVisibility = computeTileVisibility(headRotations);

			auto highestPriorityTile = std::max_element(tileVisibility.begin(), tileVisibility.end(), [](auto p1, auto p2) { return p1.first < p2.first; });
			auto maxVisibility =highestPriorityTile->first;
			auto visibilityPerQualityLevel = int(maxVisibility / (double)numQualityLevels);

			while (highestPriorityTile->first != 0)
			{
				// enhance quality of highest priority tile
				tileQuality.at(highestPriorityTile->second) = std::max(0, tileQuality.at(highestPriorityTile->second) - 1);

				// trigger transition if too much bandwidth is needed
				if (bandwidthNeededForTileQualityMap(tileQuality) > bandwidthEstimate * .75)
				{
					break;
				}

				// decrease visibility so highest priority tile differs after resort
				highestPriorityTile->first = std::max(0, highestPriorityTile->first - visibilityPerQualityLevel);

				// get most visible tile
				highestPriorityTile = std::max_element(tileVisibility.begin(), tileVisibility.end(), [](auto p1, auto p2) { return p1.first < p2.first; });
			}
		}
	}

	void stopAdaption()
	{

	}

	std::map<int, int> computeTileVisibility(const Quaternion& headRotation) const
	{
		std::map<int, int> tileVisibilityMap;

		for (int j = 0; j < SAMPLEPOINTS; j++)
			tileVisibilityMap[mapCoordToTile(fromViewportCoordToEquirectCoord(headRotation, samplePoints[j]))]++;

		return tileVisibilityMap;
	}

	auto download(int tile, int segment)
	{
		auto res = httpClient->Get(mpd->getUrl(segment, tile, tileQuality[tile]).c_str());

		bool cacheHit = res->get_header_value("X-Cache").compare(0, 3, "HIT") == 0;
		if (cacheHit)
		{
			cacheHits++;
			cacheHitBytesDownloaded += res->body.size();
		}

		totalBytesDownloaded += res->body.size();
		totalFilesDownloaded++;

		return res;
	}

	void downloadPopularTiles(int segment)
	{
		tileQuality = mpd->tilePopularity(segment);

		for (int i = 0; i < 16; i++)
			download(i, segment);
	}

	void downloadTiles(int segment, const std::map<int, int> qualities)
	{
		tileQuality = qualities;

		for (int i = 0; i < 16; i++)
			download(i, segment);

	}

	double cacheHitrate()
	{
		return cacheHits / (double)totalFilesDownloaded;
	}
	double byteHitrate()
	{
		return cacheHitBytesDownloaded / (double)totalBytesDownloaded;
	}

	void resetCacheHitrateVars()
	{
		cacheHits = 0;
		totalFilesDownloaded = 0;
		cacheHitBytesDownloaded = 0;
		totalBytesDownloaded = 0;
	}


private:
	const DASH::MPD* mpd;
	httplib::Client* httpClient;
	std::map<double, std::map<double, int>> normalizedCoordTileMapping;
	std::map<int, int> tileQuality;
	NormalizedCoordinate samplePoints[SAMPLEPOINTS];
	int cacheHits = 0;
	int totalFilesDownloaded = 0;
	size_t cacheHitBytesDownloaded = 0;
	size_t totalBytesDownloaded = 0;

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

	std::vector<std::pair<int, int>> computeTileVisibility(const CircularBuffer<std::pair<long long, Quaternion>>& headRotations) const
	{
		std::map<int, int> tileVisibilityMap;

		if (headRotations.size() == 1)
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
				timestamp + segmentDurationMs };

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
