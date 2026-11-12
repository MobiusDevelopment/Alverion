#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** Puts a shortcut in a slot, replacing what was there. The last field is the character type, one for the player's own rather than a pet's. */
inline void writeRequestShortcutReg(FWriteBuffer& buffer, const FShortcut& shortcut)
{
	buffer.writeUInt8(Opcode::requestShortcutReg);
	buffer.writeInt32(static_cast<int32>(shortcut.type));
	buffer.writeInt32(shortcut.slot);
	buffer.writeInt32(shortcut.id);
	buffer.writeInt32(shortcut.level);
	buffer.writeInt32(1);
}
}
}
