#pragma once

#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "botcraft/Game/Vector3.hpp"
#include "botcraft/AI/TemplatedBehaviourClient.hpp"

/// @brief Example of a class where we inherit
/// TemplatedBehaviourClient<T>, with this class as parameter.
/// We can then use Behaviour Trees with this class as
/// context, and do our stuff. We also override on Handle function
class HTTP_XMPP_gateway : public Botcraft::TemplatedBehaviourClient<HTTP_XMPP_gateway>
{
public:
    HTTP_XMPP_gateway(const bool use_renderer_, std::pair<int, int> resolution, int http_port);
    ~HTTP_XMPP_gateway();

    void SetScreenshot(const int w, const int h, const std::vector<uint8_t> & pixels);
    void ClearScreenshot();

protected:
#if PROTOCOL_VERSION < 759 /* < 1.19 */
    virtual void Handle(ProtocolCraft::ClientboundChatPacket& msg) override;
#else
    virtual void Handle(ProtocolCraft::ClientboundPlayerChatPacket& msg) override;
    virtual void Handle(ProtocolCraft::ClientboundSystemChatPacket& msg) override;
#endif

    void ProcessChatMsg(const std::vector<std::string>& splitted_msg);
    bool CmdGoTo(int x, int y, int z, int timeout);
    void CmdDig(int x, int y, int z);
    void CmdInteract(const Botcraft::Position & pos, int timeout);

    std::optional<Botcraft::Position> FindRandomLocation();
    std::optional<Botcraft::Position> FindObjectToInteract();
    bool Summon(const std::string & what);

    int http_port { 8080 };
    std::thread *http_handler  { nullptr };
    std::thread *brain_handler { nullptr };

    uint64_t prev_summon { 0 };
    std::atomic_uint64_t latest_action { 0 };
    std::atomic_bool finished_walking { false };
    std::atomic_bool go_now { false };
    std::set<std::tuple<int, int, int> > seen;

    std::mutex   screenshot_lock;
    std::condition_variable  screenshot_cv;
    std::vector<uint8_t> screenshot_pixels;
    int          screenshot_w { 0 };
    int          screenshot_h { 0 };
};
