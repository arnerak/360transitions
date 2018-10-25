/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/
#include <iostream>
#include <sstream>
#include "httplib.h"

std::thread* networkTraceThread;
const size_t networkTraceSampleDurMs = 250;
bool runTrace = false;
bool resetTrace = false;
std::map<std::string, std::map<size_t, size_t>> netTraces;


void printHelp()
{
	std::cout <<
		"bw [bytes/s]  - set bandwidth\n" <<
		"trace [path]  - run network trace\n" <<
		"quit          - close server\n";
	std::cout << std::endl;
}

void networkTrace(const std::map<size_t, size_t>& trace)
{
	while (runTrace)
	{
		resetTrace = false;
		for (auto& pair : trace)
		{
			httplib::bandwidth = pair.second;
			std::this_thread::sleep_for(std::chrono::milliseconds(networkTraceSampleDurMs));
			if (!runTrace || resetTrace)
				break;
		}
	}
}

void stopNetworkTrace()
{
	runTrace = false;
	if (networkTraceThread && networkTraceThread->joinable())
		networkTraceThread->join();
}

void startNetworkTrace(const std::string& path)
{
	stopNetworkTrace();

	if (netTraces.find(path) == netTraces.end())
	{
		std::ifstream traceFile(path);
		std::map<size_t, size_t> netTrace;
		int timestamp;
		while (!traceFile.eof())
		{
			traceFile >> timestamp;
			size_t add = 1500 * (1000.0 / networkTraceSampleDurMs);
			int ts = timestamp / networkTraceSampleDurMs * networkTraceSampleDurMs;
			if (netTrace.find(ts) != netTrace.end())
				netTrace[ts] += add;
			else
				netTrace[ts] = add;
		}
		netTraces[path] = netTrace;
	}

	runTrace = true;
	networkTraceThread = new std::thread(networkTrace, netTraces.at(path));
	networkTraceThread->detach();
}

void processCommand(const std::string& cmd)
{
	std::istringstream ss(cmd);
	std::string basecmd;
	ss >> basecmd;

	if (basecmd == "quit")
		exit(-1);
	else if (basecmd == "bw")
	{
		size_t bw;
		ss >> bw;
		stopNetworkTrace();
		httplib::bandwidth = bw;
	}
	else if (basecmd == "trace")
	{
		std::string path;
		ss >> path;
		startNetworkTrace(path);
	}
	else
		printHelp();
}

int main(int argc, char* argv[])
{
	using namespace httplib;

	if (argc != 2)
	{
		std::cout << "Start with www directory path as argument." << std::endl;
		return -1;
	}

	std::cout << "www directory: " << argv[1] << std::endl;

	Server sv;
	sv.set_base_dir(argv[1]);
	sv.Get("/cntrl", [](const Request& req, Response& res) {
		std::string cntrlContent;
		cntrlContent.resize(httplib::bandwidth / 10 + 1, 'c');
		res.set_content(cntrlContent, "text/plain");
	});

	sv.Get(R"(/trace/([^\s]+))", [&](const Request& req, Response& res) {
		std::string path = req.matches[1];
		startNetworkTrace(path);
		res.set_content("ok", "text/plain");
	});

	sv.Get(R"(/bw/(\d+))", [&](const Request& req, Response& res) {
		int bw = std::stoi(req.matches[1]);
		stopNetworkTrace();
		httplib::bandwidth = bw;
		res.set_content("ok", "text/plain");
	});

	sv.Get("/tracereset", [&](const Request& req, Response& res) {
		resetTrace = true;
		res.set_content("ok", "text/plain");
	});

	std::cout << "Listening on port 80" << std::endl;
	std::thread([&sv]() { sv.listen("localhost", 80); }).detach();

	while (true)
	{
		std::cout << "> ";
		std::string input;
		std::getline(std::cin, input);
		processCommand(input);
	}

	return 0;
}