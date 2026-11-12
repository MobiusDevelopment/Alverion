#include "UI/InventoryLayout.h"

#include "GameServer/WorldFlow.h"

namespace InventoryLayout
{
	namespace
	{
		// The server's paperdoll places and body part masks, from its Inventory and BodyPart.
		const FPaperdollSlot inventoryPaperdoll[] =
		{
			{ 1, 0x0040, TEXT("inventory.slot_head") },
			{ 6, 0x0400, TEXT("inventory.slot_chest") },
			{ 23, 0x2000, TEXT("inventory.slot_back") },
			{ 5, 0x0080, TEXT("inventory.slot_main_hand") },
			{ 10, 0x0200, TEXT("inventory.slot_hands") },
			{ 11, 0x0800, TEXT("inventory.slot_legs") },
			{ 12, 0x1000, TEXT("inventory.slot_feet") },
			{ 7, 0x0100, TEXT("inventory.slot_off_hand") },
			{ 8, 0x0002, TEXT("inventory.slot_ear") },
			{ 9, 0x0004, TEXT("inventory.slot_ear") },
			{ 4, 0x0008, TEXT("inventory.slot_neck") },
			{ 13, 0x0010, TEXT("inventory.slot_finger") },
			{ 14, 0x0020, TEXT("inventory.slot_finger") }
		};
	}

	TConstArrayView<FPaperdollSlot> paperdollSlots()
	{
		return inventoryPaperdoll;
	}

	const Network::FInventoryItem* wornAt(TConstArrayView<Network::FInventoryItem> items, int32 location)
	{
		for (const Network::FInventoryItem& item : items)
		{
			if (item.equipped && (item.location == location))
			{
				return &item;
			}
		}

		return nullptr;
	}

	TArray<const Network::FInventoryItem*> bagItems(TConstArrayView<Network::FInventoryItem> items)
	{
		TArray<const Network::FInventoryItem*> bag;
		for (const Network::FInventoryItem& item : items)
		{
			if (!item.equipped)
			{
				bag.Add(&item);
			}
		}

		bag.Sort([](const Network::FInventoryItem& left, const Network::FInventoryItem& right)
		{
			return (left.location != right.location) ? (left.location < right.location) : (left.objectId < right.objectId);
		});

		return bag;
	}

	TArray<TPair<uint32, int32>> moveInBag(TConstArrayView<Network::FInventoryItem> items, uint32 objectId, int32 place)
	{
		TArray<const Network::FInventoryItem*> bag = bagItems(items);
		const int32 from = bag.IndexOfByPredicate([objectId](const Network::FInventoryItem* item)
		{
			return item->objectId == objectId;
		});

		TArray<TPair<uint32, int32>> order;
		if (from == INDEX_NONE)
		{
			return order;
		}

		const Network::FInventoryItem* moving = bag[from];
		bag.RemoveAt(from);
		bag.Insert(moving, FMath::Clamp(place, 0, bag.Num()));
		for (int32 index = 0; index < bag.Num(); index++)
		{
			order.Add(TPair<uint32, int32>(bag[index]->objectId, index));
		}

		return order;
	}

	int32 unequipMask(int32 location)
	{
		for (const FPaperdollSlot& slot : inventoryPaperdoll)
		{
			if (slot.location == location)
			{
				return slot.bodyPartMask;
			}
		}

		return 0;
	}

	bool useShortcut(Network::FWorldFlow& world, int32 slot)
	{
		const Network::FShortcut* shortcut = world.shortcuts().Find(slot);
		if ((shortcut == nullptr) || (shortcut->type != Network::EShortcutType::Item))
		{
			return false;
		}

		return world.useItem(static_cast<uint32>(shortcut->id));
	}
}
