// required: libcpp-httplib-dev

#include <fstream>
#include <httplib.h>
#include <iostream>
#include <iterator>
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
				        printf("Wait for screenshot...\n");
				        rendering_manager->Unpause();
					ClearScreenshot();
					rendering_manager->Screenshot(WriteScreenshot, this);

					std::unique_lock<std::mutex> lck(screenshot_lock);
					while(screenshot_pixels.empty())
						screenshot_cv.wait(lck);

					std::string header = "P6\n" + std::to_string(screenshot_w) + "\n" + std::to_string(screenshot_h) + "\n255\n";
					std::vector<uint8_t> out(header.begin(), header.end());
					for(int y=screenshot_h - 1; y >=0; y--)
						out.insert(out.end(), screenshot_pixels.begin() + y * screenshot_w * 3, screenshot_pixels.begin() + (y + 1) * screenshot_w * 3);
				        printf("Transmit screenshot (%dx%d, %zu bytes)\n", screenshot_w, screenshot_h, out.size());

					res.set_content_provider(out.size(), "image/x-portable-pixmap", [&, out](size_t offset, size_t length, httplib::DataSink &sink) {
						printf("%zu %zu | %zu\n", offset, length, out.size());
						sink.write(reinterpret_cast<const char *>(out.data() + offset), length);
						return true;
					});
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
