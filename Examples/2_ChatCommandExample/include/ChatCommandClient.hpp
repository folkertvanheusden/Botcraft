#pragma once

#include "botcraft/Game/Vector3.hpp"
#include "botcraft/AI/TemplatedBehaviourClient.hpp"

/// @brief Example of a class where we inherit
/// TemplatedBehaviourClient<T>, with this class as parameter.
/// We can then use Behaviour Trees with this class as
/// context, and do our stuff. We also override on Handle function
class ChatCommandClient : public Botcraft::TemplatedBehaviourClient<ChatCommandClient>
{
public:
    ChatCommandClient(const bool use_renderer_, std::pair<int, int> resolution);
    ~ChatCommandClient();

protected:
#if PROTOCOL_VERSION < 759 /* < 1.19 */
    virtual void Handle(ProtocolCraft::ClientboundChatPacket& msg) override;
#else
    virtual void Handle(ProtocolCraft::ClientboundPlayerChatPacket& msg) override;
    virtual void Handle(ProtocolCraft::ClientboundSystemChatPacket& msg) override;
#endif

    void ProcessChatMsg(const std::vector<std::string>& splitted_msg);

    std::thread *http_handler { nullptr };
};
