#pragma once

#include "GameServer/GameServerProtocol.h"
#include "GameServer/ServerPackets/AppearanceTrailer.h"

namespace Network
{
namespace GameServer
{
inline bool readCharacterSelectionInfo(TArrayView<const uint8> body, TArray<FCharacterListEntry>& characters)
{
	FReadBuffer buffer(body);
	int32 count = 0;
	buffer.readInt32(count);

	// The account's character limit, then a byte.
	buffer.skip(5);

	characters.Reset();
	for (int32 slot = 0; (slot < count) && !buffer.hasFailed(); slot++)
	{
		FCharacterListEntry entry;
		FString accountName;
		int32 objectId = 0;
		int32 female = 0;
		int32 x = 0;
		int32 y = 0;
		int32 z = 0;

		entry.slot = slot;
		buffer.readString(entry.name);
		buffer.readInt32(objectId);
		buffer.readString(accountName);

		// Session id, clan, builder level.
		buffer.skip(12);

		buffer.readInt32(female);
		buffer.readInt32(entry.race);

		// Base class, server.
		buffer.skip(8);

		buffer.readInt32(x);
		buffer.readInt32(y);
		buffer.readInt32(z);

		// HP and MP as doubles, SP, experience, experience percent.
		buffer.skip(8 + 8 + 4 + 8 + 8);

		buffer.readInt32(entry.level);

		// Karma, PK and PvP kills and seven zeros, then the item worn in each paperdoll slot.
		buffer.skip(10 * 4);
		entry.worn.SetNumZeroed(paperdollSlots);
		for (int32& itemId : entry.worn)
		{
			buffer.readInt32(itemId);
		}

		buffer.readInt32(entry.hairStyle);
		buffer.readInt32(entry.hairColor);
		buffer.readInt32(entry.face);

		// Max HP and MP.
		buffer.skip(16);

		buffer.readInt32(entry.deleteSeconds);
		buffer.readInt32(entry.classId);

		// Active, enchant byte, augmentation, transformation, four pet ints, two pet doubles, vitality.
		buffer.skip(4 + 1 + (6 * 4) + 16 + 4);

		entry.objectId = static_cast<uint32>(objectId);
		entry.female = (female != 0);
		entry.location = toLocation(x, y, z);
		characters.Add(entry);
	}

	if (buffer.hasFailed())
	{
		return false;
	}

	TArray<TArray<uint8>> blobs;
	if (readAppearanceTrailer(body, blobs) && (blobs.Num() == characters.Num()))
	{
		for (int32 index = 0; index < blobs.Num(); index++)
		{
			FAppearance check;
			if (FAppearance::unpack(blobs[index], check))
			{
				characters[index].appearance = blobs[index];
			}
		}
	}

	return true;
}
}
}
