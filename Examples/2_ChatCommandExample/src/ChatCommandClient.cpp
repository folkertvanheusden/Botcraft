// required: libcpp-httplib-dev libpng-dev

#include <fstream>
#include <httplib.h>
#include <iostream>
#include <iterator>
#include <png.h>
#include <sstream>

#include "botcraft/Game/World/World.hpp"
#include "botcraft/Game/Entities/EntityManager.hpp"
#include "botcraft/Game/Entities/LocalPlayer.hpp"
#include "botcraft/Network/NetworkManager.hpp"

#include "botcraft/AI/BehaviourTree.hpp"
#include "botcraft/AI/Tasks/AllTasks.hpp"

#include "ChatCommandClient.hpp"

using namespace Botcraft;
using namespace ProtocolCraft;


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
	ChatCommandClient *c = reinterpret_cast<ChatCommandClient *>(arg);
	c->SetScreenshot(w, h, pixels);
}

ChatCommandClient::ChatCommandClient(const bool use_renderer_, std::pair<int, int> resolution) :
	TemplatedBehaviourClient<ChatCommandClient>(use_renderer_, resolution)
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

    http_handler = new std::thread([&] {
		    httplib::Server svr;

		    svr.Post("/say", [&](const auto& req, auto& res) {
					std::string what = req.get_param_value("text");
					printf("HTTP[say]: %s\n", what.c_str());
					SendChatMessage(what);
			    });

		    svr.Post("/goto", [&](const auto& req, auto& res) {
					try
					{
						int x = std::stoi(req.get_param_value("x"));
						int y = std::stoi(req.get_param_value("y"));
						int z = std::stoi(req.get_param_value("z"));
						printf("HTTP[goto]: %d,%d,%d\n", x, y, z);
						CmdGoTo(x, y, z);
					}
					catch (const std::invalid_argument&)
					{
						return;
					}
					catch (const std::out_of_range&)
					{
						return;
					}
			    });

		    svr.Get("/screenshot", [&](const httplib::Request &req, httplib::Response &res) {
					if (!rendering_manager)
						return;
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

		    svr.listen("0.0.0.0", 8080);
	    });
}

ChatCommandClient::~ChatCommandClient()
{
	http_handler->join();
	delete http_handler;
}

#if PROTOCOL_VERSION < 759 /* < 1.19 */
void ChatCommandClient::Handle(ClientboundChatPacket& msg)
{
    ManagersClient::Handle(msg);

    // Split the message
    std::istringstream ss{ msg.GetMessage().GetText() };
    const std::vector<std::string> splitted({ std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>{} });

    // Process it
    ProcessChatMsg(splitted);
}
#else
void ChatCommandClient::Handle(ClientboundPlayerChatPacket& msg)
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

void ChatCommandClient::Handle(ClientboundSystemChatPacket& msg)
{
    ManagersClient::Handle(msg);

    // Split the message
    std::istringstream ss{ msg.GetContent().GetText() };
    const std::vector<std::string> splitted({ std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>{} });

    // Process it
    ProcessChatMsg(splitted);
}
#endif

void ChatCommandClient::SetScreenshot(const int w, const int h, const std::vector<uint8_t> & pixels)
{
	std::unique_lock<std::mutex> lck(screenshot_lock);
	screenshot_w = w;
	screenshot_h = h;
	screenshot_pixels = pixels;
	screenshot_cv.notify_all();
	rendering_manager->Pause();
}

void ChatCommandClient::ClearScreenshot()
{
	std::unique_lock<std::mutex> lck(screenshot_lock);
	screenshot_pixels.clear();
}

void ChatCommandClient::CmdGoTo(int x, int y, int z)
{
        float speed_multiplier = 1.0f;
        Position target_position = Position(x, y, z);

        auto tree = Builder<ChatCommandClient>("goto tree")
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
                    .leaf("go to lambda", [=](ChatCommandClient& c) { return GoTo(c, target_position, 0, 0, 0, true, false, speed_multiplier); })
                    .leaf("go to function", GoTo, target_position, 0, 0, 0, true, false, speed_multiplier)
                    .leaf("go to std::bind", std::bind(GoTo, std::placeholders::_1, target_position, 0, 0, 0, true, false, speed_multiplier))
                    // If goto fails, say something in chat
                    .leaf(Say, "Pathfinding failed :(")
                .end()
                // Switch back to empty behaviour
                .leaf([](ChatCommandClient& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
            .end();

        SetBehaviourTree(tree);
}

void ChatCommandClient::ProcessChatMsg(const std::vector<std::string>& splitted_msg)
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
		CmdGoTo(std::stoi(splitted_msg[2]), std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]));
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

        auto tree = Builder<ChatCommandClient>("place block")
            // shortcut for composite<Sequence<ChatCommandClient>>()
            .sequence()
                .succeeder().leaf(PlaceBlock, item, pos, PlayerDiggingFace::Up, true, true, true)
                // Switch back to empty behaviour
                .leaf([](ChatCommandClient& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
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

        Position pos;
        try
        {
            pos = Position(std::stoi(splitted_msg[2]), std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]));
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        catch (const std::out_of_range&)
        {
            return;
        }

        auto tree = Builder<ChatCommandClient>("dig")
            // shortcut for composite<Sequence<ChatCommandClient>>()
            .sequence()
                .succeeder().leaf("diggy diggy hole", Dig, pos, true, PlayerDiggingFace::Up, true)
                // Switch back to empty behaviour
                .leaf([](ChatCommandClient& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
            .end();

        SetBehaviourTree(tree);
    }
    else if (splitted_msg[1] == "interact")
    {
        if (splitted_msg.size() < 5)
        {
            SendChatMessage("Usage: [BotName] [interact] [x] [y] [z]");
            return;
        }
        Position pos;
        try
        {
            pos = Position(std::stoi(splitted_msg[2]), std::stoi(splitted_msg[3]), std::stoi(splitted_msg[4]));
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        catch (const std::out_of_range&)
        {
            return;
        }

        auto tree = Builder<ChatCommandClient>("interact")
            // shortcut for composite<Sequence<ChatCommandClient>>()
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
                .leaf([](ChatCommandClient& c) { c.SetBehaviourTree(nullptr); return Status::Success; })
            .end();

        SetBehaviourTree(tree);
    }
}
