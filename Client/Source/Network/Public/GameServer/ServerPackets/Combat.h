#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** The hit flags Attack.java sets. A blow that missed carries no others. */
inline constexpr uint8 hitMissed = 0x80;
inline constexpr uint8 hitBlocked = 0x40;
inline constexpr uint8 hitCritical = 0x20;

/** One hit as Attack.java writes it: the target, the damage, then the flags. */
inline void readAttackHit(FReadBuffer& buffer, FAttackHit& hit)
{
	int32 target = 0;
	buffer.readInt32(target);
	buffer.readInt32(hit.damage);
	buffer.readUInt8(hit.flags);
	hit.targetId = static_cast<uint32>(target);
}

/** Attack: the attacker, its first hit, where the attacker stands, how many more hits and those hits, then where the target stands. */
inline bool readAttack(TArrayView<const uint8> body, FAttack& attack)
{
	FReadBuffer buffer(body);
	int32 attacker = 0;
	buffer.readInt32(attacker);
	attack.attackerId = static_cast<uint32>(attacker);
	attack.hits.Reset();
	readAttackHit(buffer, attack.hits.AddDefaulted_GetRef());

	buffer.skip(3 * 4);
	uint16 more = 0;
	buffer.readUInt16(more);
	for (int32 index = 0; (index < more) && !buffer.hasFailed(); index++)
	{
		readAttackHit(buffer, attack.hits.AddDefaulted_GetRef());
	}

	return !buffer.hasFailed();
}

/** Die, Revive, AutoAttackStart and AutoAttackStop all lead with the object they are about. Die's restart options follow, and the village is the only one this client offers. */
inline bool readCombatObjectId(TArrayView<const uint8> body, uint32& objectId)
{
	FReadBuffer buffer(body);
	int32 value = 0;
	if (!buffer.readInt32(value))
	{
		return false;
	}

	objectId = static_cast<uint32>(value);
	return true;
}
}
}
