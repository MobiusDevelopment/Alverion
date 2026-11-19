#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** What a loot request does with a corpse. */
enum class ELootAction : uint8
{
	Look = 0,
	Take = 1,
	TakeAll = 2
};

/**
 * Loots a corpse through the Alverion server patch, which answers with the corpse's loot list.
 * A take names the stack's place in the list and its item, so the server can tell the stack is still the one this client saw when somebody else looted meanwhile. The other actions send both as zero.
 */
inline void writeRequestCorpseLoot(FWriteBuffer& buffer, ELootAction action, uint32 corpseId, int32 index, int32 itemId)
{
	buffer.writeUInt8(Opcode::extended);
	buffer.writeInt16(Opcode::requestCorpseLoot);
	buffer.writeUInt8(static_cast<uint8>(action));
	buffer.writeInt32(static_cast<int32>(corpseId));
	buffer.writeInt32(index);
	buffer.writeInt32(itemId);
}
}
}
