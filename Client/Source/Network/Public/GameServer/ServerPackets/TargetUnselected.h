#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** Somebody let go of their target. The object it carries is the one who let go, so this client's own id means its own target is gone. */
inline bool readTargetUnselected(FReadBuffer& buffer, uint32& objectId)
{
	int32 value = 0;
	buffer.readInt32(value);
	if (buffer.hasFailed())
	{
		return false;
	}

	objectId = static_cast<uint32>(value);
	return true;
}
}
}
