#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
#pragma once
#include "protocolCraft/NetworkType.hpp"
#include "protocolCraft/Types/Identifier.hpp"
#include "protocolCraft/Types/Chat/Chat.hpp"

namespace ProtocolCraft
{
    namespace Components
    {
        class TrimPattern : public NetworkType
        {
            SERIALIZED_FIELD(AssetId, Identifier);
#if PROTOCOL_VERSION < 770 /* < 1.21.5 */
            SERIALIZED_FIELD(TemplateItem, VarInt);
#endif
            SERIALIZED_FIELD(Description, Chat);
            SERIALIZED_FIELD(Decal, bool);

            DECLARE_READ_WRITE_SERIALIZE;
        };
    }
}
#endif
