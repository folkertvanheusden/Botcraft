// required: libcpp-httplib-dev libpng-dev

#include <fstream>
#include <httplib.h>
#include <iostream>
#include <iterator>
#include <png.h>
#include <sstream>
#include <sys/time.h>

#include "botcraft/Game/World/World.hpp"
#include "botcraft/Game/Entities/EntityManager.hpp"
#include "botcraft/Game/Entities/LocalPlayer.hpp"
#include "botcraft/Network/NetworkManager.hpp"
#include "botcraft/Utilities/Logger.hpp"
#include "botcraft/AI/BehaviourTree.hpp"
#include "botcraft/AI/Tasks/AllTasks.hpp"

#include "HTTP_XMPP_gateway.hpp"

using namespace Botcraft;
using namespace ProtocolCraft;

// #define TURBO

#if defined(NDEBUG) && !defined(TURBO)
#define MIN_SLEEP 29000
#define MAX_SLEEP 301000
#else
#define MIN_SLEEP 2000
#define MAX_SLEEP 3000
#endif

void set_thread_name(const std::string & name)
{
        std::string full_name = "IV:" + name;

        if (full_name.length() > 15)
                full_name = full_name.substr(0, 15);

        pthread_setname_np(pthread_self(), full_name.c_str());
}

void libpng_error_handler(png_structp png, png_const_charp msg)
{
        printf("libpng error: %s\n", msg);
}

void libpng_warning_handler(png_structp png, png_const_charp msg)
{
        printf("libpng warning: %s\n", msg);
}

void write_PNG_file(FILE *fh, int ncols, int nrows, unsigned char *pixels)
{
        png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * nrows);
        if (!row_pointers)
		return;
        for(int y=0; y<nrows; y++)
                row_pointers[y] = &pixels[y*ncols*3];

        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, libpng_error_handler, libpng_warning_handler);
        if (!png) {
		free(row_pointers);
		return;
	}

        png_infop info = png_create_info_struct(png);
        if (info == nullptr) {
		free(row_pointers);
		return;
	}

        png_init_io(png, fh);

        png_set_compression_level(png, 3);

        png_set_IHDR(png, info, ncols, nrows, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        png_text text_ptr[2];
        text_ptr[0].key = (png_charp)"Author";
        text_ptr[0].text = (png_charp)"HTTPtoMinecraft";
        text_ptr[0].compression = PNG_TEXT_COMPRESSION_NONE;
        text_ptr[1].key = (png_charp)"URL";
        text_ptr[1].text = (png_charp)"http://www.komputilo.nl/";
        text_ptr[1].compression = PNG_TEXT_COMPRESSION_NONE;
        png_set_text(png, info, text_ptr, 2);

        png_write_info(png, info);

        png_write_image(png, row_pointers);

        png_write_end(png, nullptr);

        png_destroy_write_struct(&png, &info);

        free(row_pointers);
}

void WriteScreenshot(const int w, const int h, const std::vector<uint8_t> & pixels, void *arg)
{
	HTTP_XMPP_gateway *c = reinterpret_cast<HTTP_XMPP_gateway *>(arg);
	c->SetScreenshot(w, h, pixels);
}

std::optional<std::tuple<int, int, int> > get_coordinate(const httplib::Request &req)
{
	try
	{
		int x = std::stoi(req.get_param_value("x"));
		int y = std::stoi(req.get_param_value("y"));
		int z = std::stoi(req.get_param_value("z"));
		return { { x, y, z } };
	}
	catch (const std::invalid_argument&)
	{
		printf("invalid argument\n");
	}
	catch (const std::out_of_range&)
	{
		printf("argument out of range\n");
	}

	return { };
}

std::optional<double> get_angle(const httplib::Request &req)
{
	try
	{
		double a = std::stod(req.get_param_value("angle"));
		return { a };
	}
	catch (const std::invalid_argument&)
	{
		printf("invalid argument\n");
	}
	catch (const std::out_of_range&)
	{
		printf("argument out of range\n");
	}

	return { };
}

uint64_t GetMs()
{
        timeval tv { };
        gettimeofday(&tv, nullptr);

        return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

HTTP_XMPP_gateway::HTTP_XMPP_gateway(const bool use_renderer_, std::pair<int, int> resolution, int http_port) :
	TemplatedBehaviourClient<HTTP_XMPP_gateway>(use_renderer_, resolution), http_port(http_port)
{
    std::cout << "Known commands:\n";
    std::cout << "    Pathfinding to position:\n";
    std::cout << "        name goto x y z (speed_multiplier=1.0)\n";
    std::cout << "    Stop what you're doing:\n";
    std::cout << "        name stop\n";
    std::cout << "    Place a block:\n";
    std::cout << "        name place_block minecraft:item x y z\n";
    std::cout << "    Break a block:\n";
    std::cout << "        name dig x y z\n";
    std::cout << "    Interact (right click) a block:\n";
    std::cout << "        name interact x y z\n";
    std::cout << "    Screen shot:\n";
    std::cout << "        name screenshot\n";

    srand(GetMs());

    latest_action = GetMs();

    http_handler = new std::thread([&] {
		    set_thread_name("HTTP_handler");
		    httplib::Server svr;

		    svr.Post("/say", [&](const auto& req, auto& res) {
					latest_action = GetMs();
					std::string what = req.get_param_value("text");
					LOG_INFO("HTTP[say]: " << what);
					SendChatMessage(what);
			    });

		    svr.Post("/interact", [&](const auto& req, auto& res) {
					latest_action = GetMs();
					auto pos = get_coordinate(req);
					if (pos.has_value()) {
						printf("HTTP[interact]: %d,%d,%d\n", std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()));
						CmdInteract(std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()));
					}
			    });

		    svr.Post("/dig", [&](const auto& req, auto& res) {
				    latest_action = GetMs();
				    auto pos = get_coordinate(req);
				    if (pos.has_value()) {
					printf("HTTP[dig]: %d,%d,%d\n", std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()));
					CmdDig(std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()));
				    }
			    });

		    svr.Post("/goto", [&](const auto& req, auto& res) {
				    latest_action = GetMs();
				    auto pos = get_coordinate(req);
				    if (pos.has_value()) {
					printf("HTTP[goto]: %d,%d,%d\n", std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()));
					CmdGoTo(std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()), -1);
				    }
			    });

		    svr.Post("/rotate", [&](const auto& req, auto& res) {
				    latest_action = GetMs();
				    auto angle = get_angle(req);
				    if (angle.has_value()) {
					printf("HTTP[rotate]: %f\n", angle.value());
			                std::shared_ptr<LocalPlayer> local_player = entity_manager->GetLocalPlayer();
					double pitch = local_player->GetYaw() + angle.value();
					local_player->SetYaw(pitch);
				    }
			    });

		    svr.Post("/look-at", [&](const auto& req, auto& res) {
				    latest_action = GetMs();
				    auto pos = get_coordinate(req);
				    if (pos.has_value()) {
					printf("HTTP[look-at]: %d,%d,%d\n", std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value()));
			                std::shared_ptr<LocalPlayer> local_player = entity_manager->GetLocalPlayer();
					local_player->LookAt(Vector3<double>(std::get<0>(pos.value()), std::get<1>(pos.value()), std::get<2>(pos.value())), true);
				    }
			    });

		    svr.Get("/state", [&](const httplib::Request &req, httplib::Response &res) {
				    latest_action = GetMs();
				    std::shared_ptr<LocalPlayer> local_player = entity_manager->GetLocalPlayer();

				    std::string state = "{ \"x\": " + std::to_string(local_player->GetX()) + ", " +
						          "\"y\": " + std::to_string(local_player->GetY()) + ", " +
						          "\"z\": " + std::to_string(local_player->GetZ()) + ", "
						          "\"pitch\": " + std::to_string(local_player->GetPitch()) + ", "
						          "\"yaw\": " + std::to_string(local_player->GetYaw()) + ", "
						          "\"is-on-ground\": " + (local_player->GetOnGround() ? "true":"false") + ", "
						          "\"is-flying\": " + (local_player->GetFlying() ? "true":"false") + ", "
						          "\"may-fly\": " + (local_player->GetMayFly() ? "true":"false") + ", "
						          "\"is-climbing\": " + (local_player->IsClimbing() ? "true":"false") + ", "
						          "\"is-in-water\": " + (local_player->IsInWater() ? "true":"false") + ", "
						          "\"is-in-lava\": " + (local_player->IsInLava() ? "true":"false") + ", "
						          "\"is-in-fluid\": " + (local_player->IsInFluid() ? "true":"false") + ", "
						          "\"insta-build\": " + (local_player->GetInstabuild() ? "true":"false") + ", "
						          "\"may-build\": " + (local_player->GetMayBuild() ? "true":"false") + ", "
						          "\"flying-speed\": " + std::to_string(local_player->GetFlyingSpeed()) + ", "
						          "\"walking-speed\": " + std::to_string(local_player->GetWalkingSpeed()) + ", "
						          "\"health\": " + std::to_string(local_player->GetHealth()) + ", "
						          "\"food-saturation\": " + std::to_string(local_player->GetFoodSaturation()) + ", "
						          "\"food\": " + std::to_string(local_player->GetFood()) +
							 "}";

				    res.set_content(state, "application/json");

			    });

		    svr.Get("/screenshot", [&](const httplib::Request &req, httplib::Response &res) {
					if (!rendering_manager)
						return;
				        latest_action = GetMs();
				        printf("Wait for screenshot...\n");
				        rendering_manager->Unpause();
					ClearScreenshot();
					std::shared_ptr<LocalPlayer> local_player = entity_manager->GetLocalPlayer();
					local_player->SetPitch(0);  // look forward
					usleep(501000);  // give bot time to lift its head
					rendering_manager->Screenshot(WriteScreenshot, this);

					std::unique_lock<std::mutex> lck(screenshot_lock);
					while(screenshot_pixels.empty())
						screenshot_cv.wait(lck);

					std::vector<uint8_t> temp;
					for(int y=screenshot_h - 1; y>=0; y--)
						temp.insert(temp.end(), screenshot_pixels.begin() + y * screenshot_w * 3, screenshot_pixels.begin() + (y + 1) * screenshot_w * 3);

					char  *data_out     = nullptr;
					size_t data_out_len = 0;
					FILE  *fh           = open_memstream(&data_out, &data_out_len);
					if (!fh)
						return;
					write_PNG_file(fh, screenshot_w, screenshot_h, temp.data());
					fclose(fh);

				        printf("Transmit screenshot (%dx%d, %zu bytes)\n", screenshot_w, screenshot_h, data_out_len);

					res.set_content_provider(data_out_len, "image/png",
						[&, data_out](size_t offset, size_t length, httplib::DataSink &sink) {
							printf("%zu %zu | %zu\n", offset, length, data_out_len);
							sink.write(data_out + offset, length);
							return true;
						},
						[data_out](bool success) { free(data_out); }
					);
			    });

		    printf("Starting HTTP server on port %d\n", this->http_port);
		    svr.listen("0.0.0.0", this->http_port);
	    });

    brain_handler = new std::thread([&] {
		        set_thread_name("brain_handler");
                        for(;;) {
                                if (entity_manager && entity_manager->GetLocalPlayer())
                                        break;
                                LOG_INFO("Waiting to get ready...");
                                usleep(101000);
                        }

			uint64_t prev_summon = 0;

			bool first = true;
			for(;;) {
				int sleep_time = (first == false ? rand() % MAX_SLEEP : 0) + MIN_SLEEP;
				first = false;
				printf("Sleeping for %.3f seconds\n", sleep_time / 1000.);
				uint64_t until_bored = GetMs() + sleep_time;
				do {
					usleep(101000);
				}
				while(GetMs() < until_bored);

				LOG_INFO("bored!");
				int activity_time = (rand() % 150000) + 1;
				uint64_t max_until = activity_time + GetMs();
				printf("Walking around for %.3f seconds\n", activity_time / 1000.);
				while(GetMs() < max_until) {
					auto local_player = entity_manager->GetLocalPlayer();

					int x = local_player->GetX();
					int y = local_player->GetY();
					int z = local_player->GetZ();
					int newx = 0, newy = 0, newz = 0;
					bool ok = false;
					for(int i=0; i<16; i++) {
						newx = x + (rand() % 200) - 100;
						newy = y + (rand() %   5) -   1;
						newz = z + (rand() % 200) - 100;
						if (seen.find({ newx, newy, newz }) == seen.end()) {
							ok = true;
							break;
						}
					}

					if (!ok) {
						LOG_INFO("No new place to go to");
						break;
					}

					std::string summon;
                                        if (CmdGoTo(newx, newy, newz, 25000))
						summon = "summon minecraft:bird ~ ~ ~";
					else {
						if (local_player->IsInWater())
						       summon = "summon minecraft:tropical_fish ~ ~ ~";
						else
						       summon = "summon minecraft:cat ~ ~ ~";
					}

					if (summon.empty() == false) {
					       uint64_t now = GetMs();
					       if (now - prev_summon >= 31000) {
						       SendChatCommand(summon);
						       prev_summon = now;
						       LOG_INFO("Summon");
					       }
					}

				        CmdInteract(local_player->GetX() + (rand() % 3) - 1, local_player->GetY() + (rand() % 3) - 1, local_player->GetZ() + (rand() % 3) - 1);
				}

				latest_action = GetMs();
			}
	    });
}

HTTP_XMPP_gateway::~HTTP_XMPP_gateway()
{
	http_handler->join();
	delete http_handler;

	brain_handler->join();
	delete brain_handler;
}

#if PROTOCOL_VERSION < 759 /* < 1.19 */
void HTTP_XMPP_gateway::Handle(ClientboundChatPacket& msg)
{
    ManagersClient::Handle(msg);

    // Split the message
    std::istringstream ss{ msg.GetMessage().GetText() };
    const std::vector<std::string> splitted({ std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>{} });

    // Process it
    ProcessChatMsg(splitted);
}
#else
void HTTP_XMPP_gateway::Handle(ClientboundPlayerChatPacket& msg)
{
    ManagersClient::Handle(msg);

    // Split the message
#if PROTOCOL_VERSION == 759 /* 1.19 */
    std::istringstream ss{ msg.GetSignedContent().GetText() };
#elif PROTOCOL_VERSION == 760 /* 1.19.1/2 */
    std::istringstream ss{ msg.GetMessage_().GetSignedBody().GetContent().GetPlain() };
#else
    std::istringstream ss{ msg.GetBody().GetContent() };
#endif
    const std::vector<std::string> splitted({ std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>{} });

    // Process it
    ProcessChatMsg(splitted);
}

void HTTP_XMPP_gateway::Handle(ClientboundSystemChatPacket& msg)
{
    ManagersClient::Handle(msg);

    // Split the message
    std::istringstream ss{ msg.GetContent().GetText() };
    const std::vector<std::string> splitted({ std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>{} });

    // Process it
    ProcessChatMsg(splitted);
}
#endif

void HTTP_XMPP_gateway::SetScreenshot(const int w, const int h, const std::vector<uint8_t> & pixels)
{
	std::unique_lock<std::mutex> lck(screenshot_lock);
	screenshot_w = w;
	screenshot_h = h;
	screenshot_pixels = pixels;
	screenshot_cv.notify_all();
	rendering_manager->Pause();
}

void HTTP_XMPP_gateway::ClearScreenshot()
{
	std::unique_lock<std::mutex> lck(screenshot_lock);
	screenshot_pixels.clear();
}

void HTTP_XMPP_gateway::CmdDig(int x, int y, int z)
{
        auto tree = Builder<HTTP_XMPP_gateway>("dig")
            // shortcut for composite<Sequence<HTTP_XMPP_gateway>>()
            .sequence()
                .succeeder().leaf("diggy diggy hole", Dig, Position(x, y, z), true, PlayerDiggingFace::Up, true)
                // Switch back to empty behaviour
                .leaf([](HTTP_XMPP_gateway& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
            .end();

        SetBehaviourTree(tree);
}

void HTTP_XMPP_gateway::CmdInteract(int x, int y, int z)
{
	Position pos(x, y, z);
        auto tree = Builder<HTTP_XMPP_gateway>("interact")
            // shortcut for composite<Sequence<HTTP_XMPP_gateway>>()
            .sequence()
                .succeeder().sequence()
                    .leaf("go next to block", GoTo, pos, 4, 0, 1, true, false, 1.0f)
                    // Set interaction position in the blackboard
                    .leaf(SetBlackboardData<Position>, "InteractWithBlock.pos", pos)
                    .selector()
                        // Perform action using the data in the blackboard
                        .leaf("interact with block", InteractWithBlockBlackboard)
                        // Say something if it fails
                        .leaf(Say, "Interacting failed :(")
                    .end()
                    // Remove interaction position in the blackboard because
                    // we don't want to leave a mess (and to show how to do it)
                    .leaf(RemoveBlackboardData, "InteractWithBlock.pos")
                .end()
                // Switch back to empty behaviour
                .leaf([](HTTP_XMPP_gateway& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
            .end();

        SetBehaviourTree(tree);
}

bool HTTP_XMPP_gateway::CmdGoTo(int x, int y, int z, int timeout)
{
        float speed_multiplier = 1.0f;
        Position target_position = Position(x, y, z);

        auto tree = Builder<HTTP_XMPP_gateway>("goto tree")
            .sequence()
                // Perform the pathfinding in a Selector,
                // so it exits as soon as one leaf
                // returns success
                .selector()
                    // The next three lines do exactly the same,
                    // they're only here to show the different
                    // possibilities to create a leaf. Note that
                    // only the lambda solution can use default
                    // parameters values
                    .leaf("go to lambda", [=](HTTP_XMPP_gateway& c) { return GoTo(c, target_position, 0, 0, 0, true, false, speed_multiplier); })
                    .leaf("go to function", GoTo, target_position, 0, 0, 0, true, false, speed_multiplier)
                    .leaf("go to std::bind", std::bind(GoTo, std::placeholders::_1, target_position, 0, 0, 0, true, false, speed_multiplier))
                .end()
                // Switch back to empty behaviour
                .leaf([&](HTTP_XMPP_gateway& c) { c.SetBehaviourTree(nullptr); finished_walking = true; return Status::Success; })
            .end();

        SetBehaviourTree(tree);

	if (timeout < 0)  // negative is async
		return true;

        auto start = GetMs();
        while(!finished_walking && GetMs() < start + timeout)
                usleep(101000);  // TODO: cv

        SetBehaviourTree(nullptr);

        if (finished_walking)
                seen.insert({ x, y, z });
        else
                printf("Walking to %d,%d,%d timed out\n", x, y, z);

        return finished_walking;
}

void HTTP_XMPP_gateway::ProcessChatMsg(const std::vector<std::string>& splitted_msg)
{
    if (splitted_msg.size() < 2 || splitted_msg[0] != network_manager->GetMyName())
    {
        return;
    }

    if (splitted_msg[1] == "help")
    {
	    SendChatMessage("goto / place_block / dig / interact / screenshot");
    }
    else if (splitted_msg[1] == "goto")
    {
        if (splitted_msg.size() < 5)
        {
            SendChatMessage("Usage: [BotName] [goto] [x] [y] [z] [speed_multiplier]");
            return;
        }

	try
	{
		CmdGoTo(std::stoi(splitted_msg[2]), std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]), 15000);
	}
	catch (const std::invalid_argument&)
	{
		return;
	}
	catch (const std::out_of_range&)
	{
		return;
	}
    }
    else if (splitted_msg[1] == "screenshot")
    {
	    printf("Making screenshot\n");
	    rendering_manager->Unpause();
	    rendering_manager->Screenshot(WriteScreenshot, this);
    }
    else if (splitted_msg[1] == "stop")
    {
	SendChatMessage("Stopped");
        // Stop any running behaviour
        SetBehaviourTree(nullptr);
    }
    else if (splitted_msg[1] == "place_block")
    {
        if (splitted_msg.size() < 6)
        {
            SendChatMessage("Usage: [BotName] [place_block] [item] [x] [y] [z]");
            return;
        }
        const std::string& item = splitted_msg[2];
        Position pos;
        try
        {
            pos = Position(std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]), std::stoi(splitted_msg[5]));
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        catch (const std::out_of_range&)
        {
            return;
        }
        LOG_INFO("Asked to place a block at " << pos << " (" << item << ")");

        auto tree = Builder<HTTP_XMPP_gateway>("place block")
            // shortcut for composite<Sequence<HTTP_XMPP_gateway>>()
            .sequence()
                .succeeder().leaf(PlaceBlock, item, pos, PlayerDiggingFace::Up, true, true, true)
                // Switch back to empty behaviour
                .leaf([](HTTP_XMPP_gateway& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
            .end();

        SetBehaviourTree(tree);
    }
    else if (splitted_msg[1] == "terminate") {
	    should_be_closed = true;  // TODO
	    exit(0);
    }
    else if (splitted_msg[1] == "dig")
    {
        if (splitted_msg.size() < 5)
        {
            SendChatMessage("Usage: [BotName] [dig] [x] [y] [z]");
            return;
        }

        try
        {
	    CmdDig(std::stoi(splitted_msg[2]), std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]));
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        catch (const std::out_of_range&)
        {
            return;
        }
    }
    else if (splitted_msg[1] == "interact")
    {
        if (splitted_msg.size() < 5)
        {
            SendChatMessage("Usage: [BotName] [interact] [x] [y] [z]");
            return;
        }

        try
        {
            CmdInteract(std::stoi(splitted_msg[2]), std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]));
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        catch (const std::out_of_range&)
        {
            return;
        }
    }
}
