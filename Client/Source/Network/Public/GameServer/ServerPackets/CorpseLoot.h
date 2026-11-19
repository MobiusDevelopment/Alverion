#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** The loot list, after its extended id: the corpse, how many stacks, then each stack's item id and count. */
inline bool readCorpseLoot(TArrayView<const uint8> body, FCorpseLoot& loot)
{
	FReadBuffer buffer(body);
	int32 corpse = 0;
	int32 count = 0;
	buffer.readInt32(corpse);
	buffer.readInt32(count);
	loot.corpseId = static_cast<uint32>(corpse);
	loot.stacks.Reset();
	for (int32 index = 0; (index < count) && !buffer.hasFailed(); index++)
	{
		FLootStack& stack = loot.stacks.AddDefaulted_GetRef();
		buffer.readInt32(stack.itemId);
		buffer.readInt64(stack.count);
	}

	return !buffer.hasFailed();
}
}
}
