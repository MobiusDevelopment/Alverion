#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/** What InventoryUpdate says happened to an item, numbered as the server numbers it. */
enum class EItemChange : uint16
{
	Add = 1,
	Modify = 2,
	Remove = 3
};

/** One item as AbstractItemPacket writes it: 68 bytes, of which this client keeps what the bag and the paperdoll show. */
inline bool readInventoryItem(FReadBuffer& buffer, FInventoryItem& item)
{
	int32 objectId = 0;
	uint16 equipped = 0;

	buffer.readInt32(objectId);
	buffer.readInt32(item.itemId);
	buffer.readInt32(item.location);
	buffer.readInt64(item.count);
	buffer.readUInt16(item.type2);

	// The first custom type, always zero.
	buffer.skip(2);

	buffer.readUInt16(equipped);
	buffer.readInt32(item.bodyPart);
	buffer.readUInt16(item.enchant);

	// The second custom type, the augmentation, the mana, the time left, the attack element and power, six defence elements and three enchant options.
	buffer.skip(2 + 4 + 4 + 4 + 2 + 2 + (6 * 2) + (3 * 2));

	item.objectId = static_cast<uint32>(objectId);
	item.equipped = (equipped != 0);
	return !buffer.hasFailed();
}

/** ItemList: everything the character carries outside quest items, sent on entering the world and whenever the server lists it again. */
inline bool readItemList(FReadBuffer& buffer, TArray<FInventoryItem>& items)
{
	uint16 showWindow = 0;
	uint16 count = 0;

	buffer.readUInt16(showWindow);
	buffer.readUInt16(count);

	items.Reset(count);
	for (int32 index = 0; index < count; index++)
	{
		FInventoryItem item;
		if (!readInventoryItem(buffer, item))
		{
			return false;
		}

		items.Add(item);
	}

	// The inventory block follows, which this client does not use.
	return !buffer.hasFailed();
}

/** InventoryUpdate: items added, changed or removed since the last list. */
inline bool readInventoryUpdate(FReadBuffer& buffer, TArray<TPair<EItemChange, FInventoryItem>>& changes)
{
	uint16 count = 0;
	buffer.readUInt16(count);

	changes.Reset(count);
	for (int32 index = 0; index < count; index++)
	{
		uint16 change = 0;
		FInventoryItem item;
		buffer.readUInt16(change);
		if (!readInventoryItem(buffer, item))
		{
			return false;
		}

		changes.Add(TPair<EItemChange, FInventoryItem>(static_cast<EItemChange>(change), item));
	}

	return !buffer.hasFailed();
}

/**
 * Brings a list up to date with an InventoryUpdate.
 * A modified item that is not listed yet is added, and a removed one that is not listed is ignored, so an update that overtakes the list does no harm.
 */
inline void applyInventoryUpdate(TArray<FInventoryItem>& items, TConstArrayView<TPair<EItemChange, FInventoryItem>> changes)
{
	for (const TPair<EItemChange, FInventoryItem>& change : changes)
	{
		const int32 index = items.IndexOfByPredicate([&change](const FInventoryItem& item)
		{
			return item.objectId == change.Value.objectId;
		});

		if (change.Key == EItemChange::Remove)
		{
			if (index != INDEX_NONE)
			{
				items.RemoveAt(index);
			}
		}
		else if (index != INDEX_NONE)
		{
			items[index] = change.Value;
		}
		else
		{
			items.Add(change.Value);
		}
	}
}
}
}
