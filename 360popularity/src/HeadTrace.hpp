/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/
#pragma once

#include "Quaternion.hpp"
#include <sstream>
#include <fstream>
#include <map>
#define M_PI           3.14159265358979323846  /* pi */
using namespace IMT;

class HeadTrace
{
public:
#ifdef _WIN32
	HeadTrace(const wchar_t* path)
#else
	HeadTrace(const char* path)
#endif
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
				Quaternion a(w, x, y, z);
				Quaternion rot = Quaternion::QuaternionFromAngleAxis(-0.5*M_PI, VectorCartesian(0, 0, 1));
				a = rot.Inv() * a;
				rotations[timestamp] = Quaternion(a.GetW(), -a.GetV().GetX(), -a.GetV().GetY(), a.GetV().GetZ());
			}
		}
	}

	auto rotationForTimestamp(double timestamp)
	{
		return rotations.lower_bound(timestamp)->second;
	}

	auto rotationForTimestampIt(double timestamp)
	{
		return rotations.lower_bound(timestamp);
	}
private:
	std::map<double, Quaternion> rotations;
};
