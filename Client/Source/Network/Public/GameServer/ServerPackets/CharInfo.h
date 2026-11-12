#pragma once

#include "GameServer/GameServerProtocol.h"
#include "GameServer/ServerPackets/AppearanceTrailer.h"

namespace Network
{
namespace GameServer
{
/** CharInfo: another player, read as far as its heading, then the extension's trailer from the end. */
inline bool readCharInfo(TArrayView<const uint8> body, FCharacterSpawn& spawn)
{
	FReadBuffer buffer(body);
	int32 x = 0;
	int32 y = 0;
	int32 z = 0;
	int32 objectId = 0;
	int32 female = 0;
	int32 runSpeed = 0;
	int32 walkSpeed = 0;
	int32 swimSpeed = 0;
	double moveMultiplier = 1.0;
	uint16 cubicCount = 0;
	int32 heading = 0;

	buffer.readInt32(x);
	buffer.readInt32(y);
	buffer.readInt32(z);

	// Vehicle.
	buffer.skip(4);

	buffer.readInt32(objectId);
	buffer.readString(spawn.name);
	buffer.readInt32(spawn.race);
	buffer.readInt32(female);

	// Base class, then the paperdoll items.
	buffer.skip(4);
	spawn.worn.SetNumZeroed(charInfoPaperdollSlots);
	for (int32& itemId : spawn.worn)
	{
		buffer.readInt32(itemId);
	}

	// Their augmentations, then talismans, cloak, PvP flag, karma, both attack speeds and an unknown.
	buffer.skip((charInfoPaperdollSlots + 7) * 4);

	buffer.readInt32(runSpeed);
	buffer.readInt32(walkSpeed);

	buffer.readInt32(swimSpeed);

	// Swim walk, flying run and walk twice over.
	buffer.skip(5 * 4);

	buffer.readDouble(moveMultiplier);

	// Attack speed multiplier, collision radius and height.
	buffer.skip(24);

	buffer.readInt32(spawn.hairStyle);
	buffer.readInt32(spawn.hairColor);
	buffer.readInt32(spawn.face);
	buffer.readString(spawn.title);

	// Clan, crest, alliance and alliance crest, then the standing byte, the running byte, and combat, dead, invisible, mount and store.
	buffer.skip(4 * 4);
	buffer.skip(1);

	uint8 running = 0;
	buffer.readUInt8(running);
	buffer.skip(5);

	buffer.readUInt16(cubicCount);
	buffer.skip(cubicCount * 2);

	// Matching room, abnormal effects, water or flying, recommendations, mount npc, class, an unknown, enchant glow, team, large crest, noble, hero, fishing, fishing spot, name colour.
	buffer.skip(1 + 4 + 1 + 2 + (3 * 4) + 1 + 1 + 4 + 1 + 1 + 1 + (4 * 4));

	buffer.readInt32(heading);

	if (buffer.hasFailed())
	{
		return false;
	}

	spawn.objectId = static_cast<uint32>(objectId);
	spawn.female = (female != 0);
	spawn.location = toLocation(x, y, z);
	spawn.yaw = headingToYaw(heading);
	spawn.running = (running != 0);
	spawn.runSpeed = static_cast<float>(toCentimetres(runSpeed) * moveMultiplier);
	spawn.walkSpeed = static_cast<float>(toCentimetres(walkSpeed) * moveMultiplier);
	spawn.swimSpeed = static_cast<float>(toCentimetres(swimSpeed) * moveMultiplier);

	TArray<TArray<uint8>> blobs;
	FAppearance check;
	if (readAppearanceTrailer(body, blobs) && (blobs.Num() == 1) && FAppearance::unpack(blobs[0], check))
	{
		spawn.appearance = blobs[0];
	}

	return true;
}
}
}
