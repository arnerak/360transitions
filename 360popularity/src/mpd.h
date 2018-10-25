/*
Author: Arne-Tobias Rak
TU Darmstadt

Wrapper class for mpd xml files
*/

#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include "tinyxml2.h"
#include <chrono>
using namespace tinyxml2;
#define getattr(elem, attr) attr = elem->Attribute(#attr) ? elem->Attribute(#attr) : ""
#define getattr_dur(elem, attr) if(elem->Attribute(#attr)) attr = parseDuration(elem->Attribute(#attr)); else attr = std::chrono::milliseconds(0);
#define getattr_int(elem, attr) attr = elem->Attribute(#attr) ? std::stoi(elem->Attribute(#attr)) : -1
#define getattr_bool(elem, attr) if(elem->Attribute(#attr)) std::istringstream(elem->Attribute(#attr)) >> std::boolalpha >> attr; else attr = false;
namespace DASH
{
	static std::chrono::duration<int, std::milli> parseDuration(std::string str)
	{
		std::chrono::duration<int, std::milli> dur = std::chrono::milliseconds(0);
		std::istringstream ss(str);
		char c; ss >> c >> c;
		while (ss.tellg() < str.size() - 1)
		{
			double d;
			ss >> d;
			ss >> c;
			switch (c)
			{
			case 'H':
				dur += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double, std::ratio<3600>>(d));
				break;
			case 'M':
				dur += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double, std::ratio<60>>(d));
				break;
			case 'S':
				dur += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(d));
				break;
			}
		}
		return dur;
	}

	static double parseFramerate(std::string str)
	{
		std::istringstream ss(str);
		char c;
		double num, denom;
		ss >> num;
		if (ss.tellg() >= str.size() - 1)
			return num;
		ss >> c >> denom;
		return num / denom;
	}

	struct SegmentList
	{
		void parse(XMLElement* elem)
		{
			getattr_int(elem, timescale);
			getattr_int(elem, duration);
			initializationUrl = elem->FirstChildElement("Initialization")->Attribute("sourceURL");

			for (auto e = elem->FirstChildElement("SegmentURL"); e != NULL; e = e->NextSiblingElement("SegmentURL"))
			{
				segmentUrls.push_back(e->Attribute("media"));
			}
		}
		uint32_t timescale;
		uint32_t duration;
		std::string initializationUrl;
		std::vector<std::string> segmentUrls;
	};

	struct Representation
	{
		void parse(XMLElement* elem)
		{
			getattr(elem, id);
			getattr_int(elem, width);
			getattr_int(elem, height);
			getattr_int(elem, bandwidth);
			getattr(elem, frameRate);
			segmentList.parse(elem->FirstChildElement("SegmentList"));
		}
		std::string id;
		uint32_t width;
		uint32_t height;
		std::string frameRate;
		uint32_t bandwidth;
		SegmentList segmentList;
	};

	struct SRD
	{
		void parse(XMLElement* elem)
		{
			if (!elem)
				return;

			std::istringstream ss(elem->Attribute("value"));
			char c;
			ss >> i >> c >> x >> c >> y >> c >> w >> c >> h >> c >> th >> c >> tv;
		}

		int32_t i, x, y, w, h, th, tv;
	};

	struct AdaptationSet
	{
		void parse(XMLElement* elem)
		{
			getattr_bool(elem, segmentAlignment);
			srd.parse(elem->FirstChildElement("SupplementalProperty"));
			for (auto e = elem->FirstChildElement("Representation"); e != NULL; e = e->NextSiblingElement("Representation"))
			{
				representations.push_back(Representation());
				representations.back().parse(e);
			}
		}
		bool segmentAlignment;
		SRD srd;
		std::vector<Representation> representations;
	};

	struct Period
	{
		void parse(XMLElement* elem)
		{
			getattr(elem, start);
			getattr_dur(elem, duration);
			for (auto e = elem->FirstChildElement("AdaptationSet"); e != NULL; e = e->NextSiblingElement("AdaptationSet"))
			{
				adaptationSets.push_back(AdaptationSet());
				adaptationSets.back().parse(e);
			}
		}

		std::string start;
		std::chrono::duration<int, std::milli> duration;
		std::vector<AdaptationSet> adaptationSets;
	};

	struct MPD
	{
		MPD(const std::string& mpdText)
		{
			auto e = xml.Parse(mpdText.c_str(), mpdText.size());
			if (e)
				std::cout << e << " " << xml.ErrorStr() << std::endl;
			auto elem = xml.FirstChildElement();
			getattr(elem, xmlns);
			getattr_dur(elem, minBufferTime);
			getattr_dur(elem, mediaPresentationDuration);
			getattr(elem, profiles);
			period.parse(elem->FirstChildElement("Period"));
		}


		std::string getInitUrl(int adaptionSet = 0, int representation = 0) const
		{
			return "/" + period.adaptationSets.at(adaptionSet).representations.at(representation).segmentList.initializationUrl;
		}

		std::string getUrl(int segmentIndex, int adaptionSet = 0, int representation = 0) const
		{
			return "/" + period.adaptationSets.at(adaptionSet).representations.at(representation).segmentList.segmentUrls.at(segmentIndex);
		}

		double frameRate(int adaptionSet = 0, int representation = 0) const
		{
			return parseFramerate(period.adaptationSets.at(adaptionSet).representations.at(representation).frameRate);
		}

		double segmentDuration(int adaptionSet = 0, int representation = 0) const
		{
			auto segmentList = period.adaptationSets.at(adaptionSet).representations.at(representation).segmentList;
			return segmentList.duration / (double)segmentList.timescale;
		}

		XMLDocument& getXML()
		{
			return xml;
		}

		XMLDocument xml;
		std::string xmlns;
		std::chrono::duration<int, std::milli> minBufferTime;
		std::chrono::duration<int, std::milli> mediaPresentationDuration;
		std::string profiles;
		Period period;
	};
}
