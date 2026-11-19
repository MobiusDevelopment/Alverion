#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** The object id and position every place packet starts with. MoveToLocation's is the destination. */
inline bool readObjectPlace(FReadBuffer& buffer, FObjectPlace& place)
{
	int32 objectId = 0;
	int32 x = 0;
	int32 y = 0;
	int32 z = 0;

	buffer.readInt32(objectId);
	buffer.readInt32(x);
	buffer.readInt32(y);
	buffer.readInt32(z);

	place.objectId = static_cast<uint32>(objectId);
	place.location = toLocation(x, y, z);
	return !buffer.hasFailed();
}

/**
 * MoveToPawn: a creature following another, as a monster chases whoever it fights. The mover, the one it follows, how near it means to get, then where each stands.
 * Read as a destination that far short of the one followed, along the line between them, or where the mover stands when it is that near already.
 */
inline bool readMoveToPawn(FReadBuffer& buffer, FObjectPlace& place)
{
	int32 objectId = 0;
	int32 followed = 0;
	int32 distance = 0;
	int32 mover[3] = { 0, 0, 0 };
	int32 target[3] = { 0, 0, 0 };
	buffer.readInt32(objectId);
	buffer.readInt32(followed);
	buffer.readInt32(distance);
	for (int32& value : mover)
	{
		buffer.readInt32(value);
	}

	for (int32& value : target)
	{
		buffer.readInt32(value);
	}

	const FVector from = toLocation(mover[0], mover[1], mover[2]);
	const FVector to = toLocation(target[0], target[1], target[2]);
	const double stopShortCm = toCentimetres(distance);
	const FVector offset = to - from;
	place.objectId = static_cast<uint32>(objectId);
	place.location = (offset.Size2D() <= stopShortCm) ? from : to - (offset.GetSafeNormal2D() * stopShortCm);
	return !buffer.hasFailed();
}

/** StopMove and ValidateLocation end in a heading. TeleportToLocation has a fade flag before it. */
inline bool readObjectPlaceWithHeading(FReadBuffer& buffer, FObjectPlace& place, bool teleport)
{
	int32 heading = 0;

	readObjectPlace(buffer, place);
	if (teleport)
	{
		buffer.skip(4);
	}

	buffer.readInt32(heading);
	place.yaw = headingToYaw(heading);
	return !buffer.hasFailed();
}
}
}
