#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** CharacterRestore: takes back a deletion still waiting on its days, by the character's place in the list. The server answers with the list again. */
inline void writeCharacterRestore(FWriteBuffer& buffer, int32 slot)
{
	buffer.writeUInt8(Opcode::characterRestore);
	buffer.writeInt32(slot);
}
}
}
