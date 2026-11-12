#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** UserInfo up to the run speed: who this client is and the figures the character sheet shows. */
inline bool readUserInfo(FReadBuffer& buffer, FUserInfo& info)
{
	// The equipment slots the server lists, each of its three equipment blocks carrying one int per slot.
	constexpr int32 equipmentSlots = 26;

	int32 objectId = 0;
	FString name;

	// Position and vehicle.
	buffer.skip(16);
	buffer.readInt32(objectId);
	buffer.readString(name);

	// Race, sex and base class.
	buffer.skip(12);
	buffer.readInt32(info.level);

	// Experience, and the fraction of the level done.
	buffer.skip(8 + 8);

	buffer.readInt32(info.strength);
	buffer.readInt32(info.dexterity);
	buffer.readInt32(info.constitution);
	buffer.readInt32(info.intelligence);
	buffer.readInt32(info.wit);
	buffer.readInt32(info.spirit);

	buffer.readInt32(info.maxHp);
	buffer.readInt32(info.currentHp);
	buffer.readInt32(info.maxMp);
	buffer.readInt32(info.currentMp);

	// Skill points, load, load limit and the weapon flag; the object ids, item ids and augmentations of every slot; talisman slots and the cloak flag.
	buffer.skip((4 * 4) + (3 * equipmentSlots * 4) + (2 * 4));

	buffer.readInt32(info.attack);

	// Attack speed.
	buffer.skip(4);

	buffer.readInt32(info.defense);
	buffer.readInt32(info.evasion);
	buffer.readInt32(info.accuracy);
	buffer.readInt32(info.critical);
	buffer.readInt32(info.magicAttack);

	// Casting speed, and the attack speed a second time.
	buffer.skip(8);

	buffer.readInt32(info.magicDefense);

	// The PvP flag and karma.
	buffer.skip(8);

	buffer.readInt32(info.runSpeed);

	if (buffer.hasFailed())
	{
		return false;
	}

	info.objectId = static_cast<uint32>(objectId);

	// Further on, the bag's size: past the other speeds, the size multipliers, the looks, the title, the clan, the cubics and the recommendations.
	// A packet that stops short only leaves it unknown.
	FString title;
	uint16 cubics = 0;
	int16 limit = 0;
	buffer.skip((7 * 4) + (4 * 8) + (4 * 4));
	buffer.readString(title);
	buffer.skip((4 * 4) + 4 + 3 + 8);
	buffer.readUInt16(cubics);
	buffer.skip((cubics * 2) + 1 + 4 + 1 + 4 + 2 + 2 + 4);
	buffer.readInt16(limit);
	if (!buffer.hasFailed())
	{
		info.inventoryLimit = limit;
	}

	return true;
}
}
}
