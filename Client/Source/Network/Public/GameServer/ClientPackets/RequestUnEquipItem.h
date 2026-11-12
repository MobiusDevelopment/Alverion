#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** Takes off whatever is worn on a body part, named by its mask: one ear, finger or hand at a time, so a pair is never taken off together. */
inline void writeRequestUnEquipItem(FWriteBuffer& buffer, int32 bodyPartMask)
{
	buffer.writeUInt8(Opcode::requestUnEquipItem);
	buffer.writeInt32(bodyPartMask);
}
}
}
