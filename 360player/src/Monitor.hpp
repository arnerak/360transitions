/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/

#include "plot.h"
#include "llist.h"
#include "ConfigParser.hpp"

class Monitor
{
public:
	Monitor()
	{
		ll.push_back_caption("Measured Download Rate", 0, 0x0000FF, CaptionType::value);
		ll.push_back_caption("Prediction Adapt.", 1, 0xFFFFFF, CaptionType::background);
		ll.push_back_caption("Popularity Adapt.", 2, 0x7777CC, CaptionType::background);

		//populate plot parameter object
		plot_params params;
		
		params.screen_width = 800;
		params.screen_heigth = 640;
		params.plot_window_title = "Transition Monitor";
		params.font_text_path = Config::instance()->monitorttf;
		params.font_text_size = 14;
		params.caption_text_x = "Time (s)";
		params.caption_text_y = "Speed (Mbit/s)";
		params.ll = &ll;
		params.scale_x = 1;
		params.scale_y = 10;
		params.max_x = 8.5;
		params.max_y = 120;

		plot = new SDLPlot(&params);
	}

	void addsample(double timestamp, double value, bool pop = false)
	{
		ll.push_back_coord(0, timestamp, value, pop ? 0x7777CC : 0xFFFFFF);
		maxval = std::max(maxval, value * 1.25);

		//populate plot parameter object
		plot_params params;

		params.screen_width = 800;
		params.screen_heigth = 640;
		params.plot_window_title = "Transition Monitor";
		params.font_text_path = Config::instance()->monitorttf;
		params.font_text_size = 14;
		params.caption_text_x = "Time (s)";
		params.caption_text_y = "Speed (Mbit/s)";
		params.ll = &ll;
		params.scale_x = std::ceil(timestamp / 10);
		params.scale_y = std::ceil(maxval / 5);
		params.max_x = timestamp;
		params.max_y = maxval;
		
		plot->update(&params);
	}

	~Monitor()
	{
		delete plot;
	}

private:
	LinkedList ll;
	SDLPlot* plot;
	double maxval = 0;
};