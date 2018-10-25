/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/
#pragma once

#include "Quaternion.hpp"
#include <sstream>
#include <fstream>

using namespace IMT;

class HeadTrace
{
public:
	HeadTrace(const char* path)
	{
		std::ifstream file(path);

		if (file)
		{
			std::stringstream ss;
			ss << file.rdbuf();
			file.close();

			std::string line;
			while (!std::getline(ss, line).eof())
			{
				double timestamp, w, x, y, z;
				ss >> timestamp >> w >> w >> x >> y >> z;
				headRotations[timestamp] = Quaternion(w, x, y, z);
			}
		}
		else
		{
			std::cout << "Headtrace file not found!" << std::endl;
		}
	}

	const Quaternion& rotationForTimestamp(double timestamp)
	{
		return headRotations.lower_bound(timestamp)->second;
	}
private:
	std::map<double, Quaternion> headRotations;
};