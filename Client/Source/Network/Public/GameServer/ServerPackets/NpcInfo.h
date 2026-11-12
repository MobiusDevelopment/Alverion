#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** NpcInfo: a monster or a townsperson, read as far as its name. */
inline bool readNpcInfo(TArrayView<const uint8> body, FCharacterSpawn& spawn)
{
	FReadBuffer buffer(body);
	static constexpr int32 npcIdOffset = 1000000;

	int32 objectId = 0;
	int32 displayId = 0;
	int32 attackable = 0;
	int32 x = 0;
	int32 y = 0;
	int32 z = 0;
	int32 heading = 0;
	int32 runSpeed = 0;
	int32 walkSpeed = 0;
	int32 swimSpeed = 0;
	double moveMultiplier = 1.0;

	buffer.readInt32(objectId);
	buffer.readInt32(displayId);
	buffer.readInt32(attackable);
	buffer.readInt32(x);
	buffer.readInt32(y);
	buffer.readInt32(z);
	buffer.readInt32(heading);

	// An unknown, both attack speeds.
	buffer.skip(12);

	buffer.readInt32(runSpeed);
	buffer.readInt32(walkSpeed);

	buffer.readInt32(swimSpeed);

	// Swim walk, flying run and walk twice over.
	buffer.skip(5 * 4);

	buffer.readDouble(moveMultiplier);

	// Attack speed multiplier, collision radius and height, three equipment ids and the flag that shows the name.
	buffer.skip(24 + 12 + 1);

	uint8 running = 0;
	buffer.readUInt8(running);

	// Combat, dead, summoned and the client name string id.
	buffer.skip(3 + 4);

	buffer.readString(spawn.name);

	if (buffer.hasFailed())
	{
		return false;
	}

	spawn.objectId = static_cast<uint32>(objectId);
	spawn.isNpc = true;
	spawn.npcId = displayId - npcIdOffset;
	spawn.attackable = (attackable != 0);
	spawn.location = toLocation(x, y, z);
	spawn.yaw = headingToYaw(heading);
	spawn.running = (running != 0);
	spawn.runSpeed = static_cast<float>(toCentimetres(runSpeed) * moveMultiplier);
	spawn.walkSpeed = static_cast<float>(toCentimetres(walkSpeed) * moveMultiplier);
	spawn.swimSpeed = static_cast<float>(toCentimetres(swimSpeed) * moveMultiplier);

	if (spawn.name.IsEmpty())
	{
		spawn.name = FString::Printf(TEXT("Npc %d"), spawn.npcId);
	}

	return true;
}
}
}
