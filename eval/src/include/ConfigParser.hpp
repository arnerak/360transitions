/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/
#pragma once

#include <string>
#include <stdexcept>
#include "IniReader.hpp"

class Config
{
public:
	enum class PlayType { Dash, Picture };
	void init(std::string path)
	{
		INIReader ini(path);

		std::string playConfig = ini.Get("Config", "playConfig", "");
		std::string typeStr = ini.Get(playConfig, "type", "");

		if (typeStr == "dash")
		{
			playType = PlayType::Dash;
			squidAddress = ini.Get(playConfig, "squidAddress", "");
			squidPort = ini.GetInteger(playConfig, "squidPort", 3128);
			mpdUri = ini.Get(playConfig, "mpdUri", "");
			viewportPrediction = ini.GetBoolean(playConfig, "viewportPrediction", true);
			popularity = ini.GetBoolean(playConfig, "popularity", true);
			transitions = ini.GetBoolean(playConfig, "transitions", true);
			demo = ini.GetBoolean(playConfig, "demo", false);
		}
		else if (typeStr == "picture")
		{
			playType = PlayType::Picture;
			imgPath = ini.Get(playConfig, "path", "");
		}
		else
			std::invalid_argument("Config::Config: invalid play type: " + typeStr);

		headtracePath = ini.Get("Headtrace", "path", "");
		useHeadtrace = ini.GetBoolean("Headtrace", "useTrace", false);
	}

	PlayType playType;

	std::string squidAddress;
	int squidPort;
	std::string mpdUri;
	bool viewportPrediction;
	bool popularity;
	bool transitions;
	bool demo;

	std::string imgPath;

	std::string headtracePath;
	bool useHeadtrace;

	static Config* instance()
	{
		if (!_instance)
			_instance = new Config();
		return _instance;
	}

private:
	Config(){}
	
	static Config* _instance;
};
