#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** The most pairs the server reads, which drops the rest. */
inline constexpr int32 inventoryOrderLimit = 125;

/** Saves where items sit in the bag, as pairs of object id and place. Worn items are skipped by the server, so only the bag goes. */
inline void writeRequestSaveInventoryOrder(FWriteBuffer& buffer, TConstArrayView<TPair<uint32, int32>> order)
{
	const int32 count = FMath::Min(order.Num(), inventoryOrderLimit);
	buffer.writeUInt8(Opcode::extended);
	buffer.writeInt16(Opcode::requestSaveInventoryOrder);
	buffer.writeInt32(count);
	for (int32 index = 0; index < count; index++)
	{
		buffer.writeInt32(static_cast<int32>(order[index].Key));
		buffer.writeInt32(order[index].Value);
	}
}
}
}
