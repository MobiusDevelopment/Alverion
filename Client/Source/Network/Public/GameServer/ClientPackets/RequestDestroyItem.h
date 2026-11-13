#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** RequestDestroyItem: destroys this many of one carried item, worn or not. The server refuses an item that cannot be destroyed. */
inline void writeRequestDestroyItem(FWriteBuffer& buffer, uint32 objectId, int64 count)
{
	buffer.writeUInt8(Opcode::requestDestroyItem);
	buffer.writeInt32(static_cast<int32>(objectId));

	// The count is a long, little end first, as every other number on the wire.
	buffer.writeInt32(static_cast<int32>(count & 0xFFFFFFFF));
	buffer.writeInt32(static_cast<int32>(count >> 32));
}
}
}
