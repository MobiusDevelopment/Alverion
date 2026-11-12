#pragma once

#include "CoreMinimal.h"
#include "GameServer/GameServerProtocol.h"

namespace Network
{
	class FWorldFlow;
}

/**
 * Where items go on the inventory screen, apart from drawing them: the paperdoll's slots, the bag's order and the names items are shown by.
 */
namespace InventoryLayout
{
	/** One paperdoll slot: the server's paperdoll place, the body part mask that takes it off, and the string table key of its caption. */
	struct FPaperdollSlot
	{
		int32 location;
		int32 bodyPartMask;
		const TCHAR* captionKey;
	};

	/** The paperdoll in screen order: four slots left of the picture, four right of it, then the jewelry, which shows nowhere on the body. */
	ALVERION_API TConstArrayView<FPaperdollSlot> paperdollSlots();

	/** How many of paperdollSlots go left of the picture, and how many right of it. The rest are the jewelry row. */
	inline constexpr int32 sideSlotCount = 4;

	/** The bag when UserInfo has not said how big it is yet, the server's default for most characters. */
	inline constexpr int32 defaultBagSize = 80;

	/** The item worn in a paperdoll place, or null. */
	ALVERION_API const Network::FInventoryItem* wornAt(TConstArrayView<Network::FInventoryItem> items, int32 location);

	/** The bag's items in the order they sit: by place, and by object id where the server gave several the same place. */
	ALVERION_API TArray<const Network::FInventoryItem*> bagItems(TConstArrayView<Network::FInventoryItem> items);

	/** The bag's new order, as object id and place pairs for the server, once an item moves to a place. A place past the last item puts it last. */
	ALVERION_API TArray<TPair<uint32, int32>> moveInBag(TConstArrayView<Network::FInventoryItem> items, uint32 objectId, int32 place);

	/** The body part mask that takes off what is worn in a paperdoll place, or zero for a place the screen has no slot for. */
	ALVERION_API int32 unequipMask(int32 location);

	/** Uses what a bar slot points at: the item of an item shortcut. Other kinds have nothing to use them by yet, so they do nothing. */
	ALVERION_API bool useShortcut(Network::FWorldFlow& world, int32 slot);
}
