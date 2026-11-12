#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** Empties a shortcut slot. The server sends nothing back, so the client clears its own copy. */
inline void writeRequestShortcutDel(FWriteBuffer& buffer, int32 slot)
{
	buffer.writeUInt8(Opcode::requestShortcutDel);
	buffer.writeInt32(slot);
}
}
}
