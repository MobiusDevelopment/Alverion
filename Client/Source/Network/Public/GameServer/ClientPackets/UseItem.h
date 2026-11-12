#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** Uses an item the character carries: worn ones come off, wearable ones go on, and anything else is used. The second field is whether Ctrl was held, which this client never sends. */
inline void writeUseItem(FWriteBuffer& buffer, uint32 objectId)
{
	buffer.writeUInt8(Opcode::useItem);
	buffer.writeInt32(static_cast<int32>(objectId));
	buffer.writeInt32(0);
}
}
}
