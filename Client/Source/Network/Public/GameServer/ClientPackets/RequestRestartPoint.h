#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** Where a fallen character asks to get up, as RequestRestartPoint.java numbers it: zero is the nearest village. */
inline constexpr int32 restartPointVillage = 0;

/** RequestRestartPoint: a fallen character asks to be taken to a restart point and revived there. */
inline void writeRequestRestartPoint(FWriteBuffer& buffer, int32 point)
{
	buffer.writeUInt8(Opcode::requestRestartPoint);
	buffer.writeInt32(point);
}
}
}
