#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/**
 * Drops whatever this character has selected. The server answers TargetUnselected naming this character.
 * One rather than zero, since zero cancels a spell being cast before it lets go of the target.
 */
inline void writeRequestTargetCanceld(FWriteBuffer& buffer)
{
	buffer.writeUInt8(Opcode::requestTargetCanceld);
	buffer.writeInt16(1);
}
}
}
