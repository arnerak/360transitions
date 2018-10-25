/*
	Author: Arne-Tobias Rak
	TU Darmstadt

	Based on OSVR RenderManager example file
*/

// Copyright 2015 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Internal Includes
#define BOOST_TYPEOF_EMULATION
#include <osvr/RenderKit/RenderManager.h>
#include <osvr/ClientKit/Context.h>
#include <osvr/Util/EigenInterop.h>

// Library/third-party includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/glew.h>

// Standard includes
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <chrono>
#include <stdlib.h> // For exit()

// This must come after we include <GL/gl.h> so its pointer types are defined.
#include <osvr/RenderKit/GraphicsLibraryOpenGL.h>
#include <osvr/RenderKit/RenderKitGraphicsTransforms.h>

//Internal Includes
#include "Mesh.hpp"
#include "ShaderTexture.hpp"
#include "ConfigParser.hpp"
#include "Quaternion.hpp"
#include "ShaderTextureVideo.hpp"
#include "ShaderTextureStatic.hpp"
#include "MeshCubeEquiUV.hpp"
#include "VideoTileStream.hpp"
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
static std::shared_ptr<ShaderTexture> sampleShader(nullptr);
static std::shared_ptr<Mesh> roomMesh(nullptr);
static VideoTileStream* segmentStreams{ nullptr };
//static std::shared_ptr<LogWriter> logWriter(nullptr);
//static std::shared_ptr<PublisherLogMQ> publisherLogMQ(nullptr);
static int numTiles = 0;
constexpr std::chrono::system_clock::time_point zero(std::chrono::system_clock::duration::zero());
static std::chrono::system_clock::time_point global_startDisplayTime(zero);
static size_t lastDisplayedFrame(0);
static size_t lastNbDroppedFrame(0);
static bool started(false);
static CircularBuffer<std::pair<long long, Quaternion>> headRotations;
static long long startTimeEpochMs;
static bool firstSegmentDownloaded = false;

// Set to true when it is time for the application to quit.
// Handlers below that set it to true when the user causes
// any of a variety of events so that we shut down the system
// cleanly.  This only works on Windows.
static bool quit = false;

#ifdef _WIN32
// Note: On Windows, this runs in a different thread from
// the main application.
static BOOL CtrlHandler(DWORD fdwCtrlType) {
	switch (fdwCtrlType) {
		// Handle the CTRL-C signal.
	case CTRL_C_EVENT:
		// CTRL-CLOSE: confirm that the user wants to exit.
	case CTRL_CLOSE_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		quit = true;
		return TRUE;
	default:
		return FALSE;
	}
}
#endif

// This callback sets a boolean value whose pointer is passed in to
// the state of the button that was pressed.  This lets the callback
// be used to handle any button press that just needs to update state.
void myButtonCallback(void* userdata, const OSVR_TimeValue* /*timestamp*/,
	const OSVR_ButtonReport* report) {
	bool* result = static_cast<bool*>(userdata);
	*result = (report->state != 0);
}

bool SetupRendering(osvr::renderkit::GraphicsLibrary library) {
	// Make sure our pointers are filled in correctly.
	if (library.OpenGL == nullptr) {
		std::cerr << "SetupRendering: No OpenGL GraphicsLibrary, this should "
			"not happen"
			<< std::endl;
		return false;
	}

	osvr::renderkit::GraphicsLibraryOpenGL* glLibrary = library.OpenGL;

	// Turn on depth testing, so we get correct ordering.
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	return true;
}

// Callback to set up a given display, which may have one or more eyes in it
void SetupDisplay(
	void* userData //< Passed into SetDisplayCallback
	, osvr::renderkit::GraphicsLibrary library //< Graphics library context to use
	, osvr::renderkit::RenderBuffer buffers //< Buffers to use
) {
	// Make sure our pointers are filled in correctly.  The config file selects
	// the graphics library to use, and may not match our needs.
	if (library.OpenGL == nullptr) {
		std::cerr
			<< "SetupDisplay: No OpenGL GraphicsLibrary, this should not happen"
			<< std::endl;
		return;
	}
	if (buffers.OpenGL == nullptr) {
		std::cerr
			<< "SetupDisplay: No OpenGL RenderBuffer, this should not happen"
			<< std::endl;
		return;
	}

	osvr::renderkit::GraphicsLibraryOpenGL* glLibrary = library.OpenGL;

	// Clear the screen to black and clear depth
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.8f, 0, 0.1f, 1.0f);
}

// Callback to set up for rendering into a given eye (viewpoint and projection).
void SetupEye(
	void* userData //< Passed into SetViewProjectionCallback
	, osvr::renderkit::GraphicsLibrary library //< Graphics library context to use
	, osvr::renderkit::RenderBuffer buffers //< Buffers to use
	, osvr::renderkit::OSVR_ViewportDescription
	viewport //< Viewport set by RenderManager
	, osvr::renderkit::OSVR_ProjectionMatrix
	projection //< Projection matrix set by RenderManager
	, size_t whichEye //< Which eye are we setting up for?
) {
	// Make sure our pointers are filled in correctly.  The config file selects
	// the graphics library to use, and may not match our needs.
	if (library.OpenGL == nullptr) {
		std::cerr
			<< "SetupEye: No OpenGL GraphicsLibrary, this should not happen"
			<< std::endl;
		return;
	}
	if (buffers.OpenGL == nullptr) {
		std::cerr << "SetupEye: No OpenGL RenderBuffer, this should not happen"
			<< std::endl;
		return;
	}

	// Set the viewport
	glViewport(static_cast<GLint>(viewport.left),
		static_cast<GLint>(viewport.lower),
		static_cast<GLint>(viewport.width),
		static_cast<GLint>(viewport.height));
}


// Callbacks to draw things in world space.
void DrawWorld(
	void* userData //< Passed into AddRenderCallback
	, osvr::renderkit::GraphicsLibrary library //< Graphics library context to use
	, osvr::renderkit::RenderBuffer buffers //< Buffers to use
	, osvr::renderkit::OSVR_ViewportDescription
	viewport //< Viewport we're rendering into
	, OSVR_PoseState pose //< OSVR ModelView matrix set by RenderManager
	, osvr::renderkit::OSVR_ProjectionMatrix
	projection //< Projection matrix set by RenderManager
	, OSVR_TimeValue deadline //< When the frame should be sent to the screen
)
{
	if (started)
	{
		// Make sure our pointers are filled in correctly.  The config file selects
		// the graphics library to use, and may not match our needs.
		if (library.OpenGL == nullptr) {
			std::cerr
				<< "DrawWorld: No OpenGL GraphicsLibrary, this should not happen"
				<< std::endl;
			return;
		}
		if (buffers.OpenGL == nullptr) {
			std::cerr << "DrawWorld: No OpenGL RenderBuffer, this should not happen"
				<< std::endl;
			return;
		}

		osvr::renderkit::GraphicsLibraryOpenGL* glLibrary = library.OpenGL;

		std::chrono::system_clock::time_point deadlineTP(
			std::chrono::seconds{ deadline.seconds } +
			std::chrono::microseconds{ deadline.microseconds });

		auto now = std::chrono::system_clock::now();
		if (global_startDisplayTime == zero)
			global_startDisplayTime = now;// + std::chrono::milliseconds(5000);
		deadlineTP = std::chrono::system_clock::time_point(now - global_startDisplayTime);

		GLdouble projectionGL[16];
		osvr::renderkit::OSVR_Projection_to_OpenGL(projectionGL, projection);
		
		if (Config::instance()->useHeadtrace)
		{
			auto quat = headTrace->rotationForTimestamp(std::chrono::time_point_cast<std::chrono::milliseconds>(deadlineTP).time_since_epoch().count() / 1000.0);
			auto rot = Quaternion::QuaternionFromAngleAxis(-0.5*M_PI, VectorCartesian(0, 0, 1));
			quat = rot.Inv() * quat;
			pose.rotation.data[0] = quat.GetW();
			pose.rotation.data[1] = -quat.GetV().GetY();
			pose.rotation.data[2] = -quat.GetV().GetZ();
			pose.rotation.data[3] = -quat.GetV().GetX();
		}

		GLdouble viewGL[16];
		osvr::renderkit::OSVR_PoseState_to_OpenGL(viewGL, pose);

		auto q = osvr::util::fromQuat(pose.rotation);

		static bool leftEye = true;
		if (leftEye)
			headRotations.push({ TIME_NOW_EPOCH_MS - startTimeEpochMs, Quaternion(q.w(), q.z(), q.x(), -q.y()) });
		leftEye = !leftEye;

		if (firstSegmentDownloaded)
		{
			// Draw a cube with a 5-meter radius as the room we are floating in.
			auto frameInfo = roomMesh->Draw(projectionGL, viewGL, sampleShader, std::move(deadlineTP));
			//au->printTileVisibility(Quaternion(q.w(), q.z(), q.x(), -q.y()));

			lastDisplayedFrame = frameInfo.m_frameDisplayId;
			lastNbDroppedFrame += frameInfo.m_nbDroppedFrame;

			if (frameInfo.m_last)
				quit = true;
		}
	}
}

void querySegmentThread()
{
	while (headRotations.size() < 1)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	au->initAdaption(headRotations[0]);
	for (int i = 0; i < numTiles; i++)
	{
		auto initRes = httpClient->Get((mpd->getInitUrl(i)).c_str());
		auto fsRes = au->download(i, 0);
		segmentStreams[i].init(mpd->period.adaptationSets[i].srd, initRes->body, fsRes->body);
		segmentStreams[i].addQuality(0, au->getCurrentTileQuality().at(i));
	}
	au->stopAdaption();

	sampleShader = std::make_shared<ShaderTextureVideo>(segmentStreams, numTiles, -1, 150, 0);
	firstSegmentDownloaded = true;

	while (headRotations.size() < headRotations.capacity())
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	int numSegments = mpd->period.adaptationSets[0].representations[0].segmentList.segmentUrls.size();
	double frameRate = mpd->frameRate();
	double segmentDuration = mpd->segmentDuration();
	double segmentFrames = segmentDuration * frameRate;

	for (int i = 1; i < numSegments; i++)
	{
		int firstSegmentFrame = i * segmentFrames;

		while (firstSegmentFrame - segmentFrames > lastDisplayedFrame)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));

		auto tileDownloadOrder = au->startAdaption(headRotations, i);
		assert(tileDownloadOrder.size() == numTiles);
		for (int t = 0; t < numTiles; t++)
		{
			int tileIndex = tileDownloadOrder[t];
			auto res = au->download(tileIndex, i);
			segmentStreams[tileIndex].addSegment(res->body, i == numSegments - 1);
			segmentStreams[tileIndex].addQuality(i * segmentDuration, au->getCurrentTileQuality().at(tileIndex));
		}
		au->stopAdaption();
	}
}

#ifdef _WIN32
#undef main
#endif
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

	if (config->playType == Config::PlayType::Dash)
	{
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
		segmentStreams = new VideoTileStream[numTiles];

		std::thread(&querySegmentThread).detach();
	}
	else if (config->playType == Config::PlayType::Picture)
	{
		sampleShader = std::make_shared<ShaderTextureStatic>(config->imgPath);
		firstSegmentDownloaded = true;
	}

	startTimeEpochMs = TIME_NOW_EPOCH_MS;

	if (config->useHeadtrace)
	{
		headTrace = new HeadTrace(config->headtracePath.c_str());
	}

	try
	{
		roomMesh = std::make_shared<MeshCubeEquiUV>(5.0f, 6 * 2 * 30 * 30);
		
		// Get an OSVR client context to use to access the devices that we need.
		osvr::clientkit::ClientContext context("com.osvr.renderManager.openGLExample");

		// Open OpenGL and set up the context for rendering to an HMD.  Do this using the OSVR RenderManager interface,
		// which maps to the nVidia or other vendor direct mode to reduce the latency.
		std::shared_ptr<osvr::renderkit::RenderManager> render(osvr::renderkit::createRenderManager(context.get(), "OpenGL"));
		
		if ((render == nullptr) || (!render->doingOkay())) {
			std::cerr << "Could not create RenderManager" << std::endl;
			return 1;
		}

		// Set callback to handle setting up rendering in an eye 
		render->SetViewProjectionCallback(SetupEye);

		// Set callback to handle setting up rendering in a display
		render->SetDisplayCallback(SetupDisplay);

		// Register callback to render things in world space.
		render->AddRenderCallback("/", DrawWorld);

		// Set up a handler to cause us to exit cleanly.
#ifdef _WIN32
		SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
#endif

		// Open the display and make sure this worked.
		osvr::renderkit::RenderManager::OpenResults ret = render->OpenDisplay();
		
		if (ret.status == osvr::renderkit::RenderManager::OpenStatus::FAILURE) 
		{
			std::cerr << "Could not open display" << std::endl;
			return 2;
		}
		if (ret.library.OpenGL == nullptr) 
		{
			std::cerr << "Attempted to run an OpenGL program with a config file that specified a different rendering library." << std::endl;
			return 3;
		}

		// Set up the rendering state we need.
		if (!SetupRendering(ret.library)) {
			return 3;
		}

		glewExperimental = true;
		if (glewInit() != GLEW_OK) {
			std::cerr << "Failed to initialize GLEW\n" << std::endl;
			return -1;
		}
		// Clear any GL error that Glew caused.  Apparently on Non-Windows platforms, this can cause a spurious  error 1280.
		glGetError();

		global_startDisplayTime = zero;
		started = true;

		std::cout << "Start playing the video\n";

		// Frame timing
		size_t countFrames = 0;
		size_t startDisplayedFrame = lastDisplayedFrame;
		auto startTime = std::chrono::system_clock::now();

		// Continue rendering until it is time to quit.
		while (!quit) 
		{
			// Update the context so we get our callbacks called and update tracker state.
			context.update();
			
			if (!render->Render()) 
			{
				std::cerr << "Render() returned false, maybe because it was asked to quit" << std::endl;
				quit = true;
			}
			// Print timing info
			auto nowTime = std::chrono::system_clock::now();
			auto duration = nowTime - startTime;
			++countFrames;
			constexpr std::chrono::seconds twoSeconds(2);
			if (duration >= twoSeconds)
			{
				std::string message = "Rendering at "
					+ std::to_string(countFrames / std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1>>>(duration).count())
					+ " fps | Video displayed at "
					+ std::to_string((lastDisplayedFrame - startDisplayedFrame - lastNbDroppedFrame) / std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1>>>(duration).count())
					+ " fps | nb droppped frame: "
					+ std::to_string(lastNbDroppedFrame)
					;
				//std::cout << "\033[2K\r" << message << std::flush;
				//std::cout << message << std::endl;
				//publisherLogMQ->SendMessage(FPS_INFO, message);
				startTime = nowTime;
				startDisplayedFrame = lastDisplayedFrame;
				lastNbDroppedFrame = 0;
				countFrames = 0;
			}
			//sleep(1);
		}
	}
	catch (std::exception& e)
	{
		std::cerr << "Uncatched exception: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Uncatched exception" << std::endl;
		return 1;
	}

	delete mpd;
	delete httpClient;

	system("PAUSE");

	return 0;
}
