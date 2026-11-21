#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/AutomationTest.h"
#include "UI/InventoryLayout.h"
#include "UI/InventoryWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryLayoutTest, "Alverion.UI.InventoryLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	Network::FInventoryItem inventoryLayoutTestItem(uint32 objectId, int32 location, bool equipped)
	{
		Network::FInventoryItem item;
		item.objectId = objectId;
		item.itemId = 100 + static_cast<int32>(objectId);
		item.location = location;
		item.count = 1;
		item.equipped = equipped;
		return item;
	}
}

bool FInventoryLayoutTest::RunTest(const FString& parameters)
{
	// Three in the bag, two of them given the same place, and a ring worn on the right finger.
	TArray<Network::FInventoryItem> items;
	items.Add(inventoryLayoutTestItem(30, 2, false));
	items.Add(inventoryLayoutTestItem(20, 0, false));
	items.Add(inventoryLayoutTestItem(10, 0, false));
	items.Add(inventoryLayoutTestItem(40, 13, true));

	const TArray<const Network::FInventoryItem*> bag = InventoryLayout::bagItems(items);
	if (!TestEqual(TEXT("The bag holds what is not worn"), bag.Num(), 3))
	{
		return false;
	}

	TestTrue(TEXT("By place, then by object id"), (bag[0]->objectId == 10) && (bag[1]->objectId == 20) && (bag[2]->objectId == 30));

	const Network::FInventoryItem* ring = InventoryLayout::wornAt(items, 13);
	TestTrue(TEXT("The ring is on the right finger"), (ring != nullptr) && (ring->objectId == 40));
	TestNull(TEXT("Nothing is on the left finger"), InventoryLayout::wornAt(items, 14));

	// The first item moved to the last place, and the others closing up behind it.
	const TArray<TPair<uint32, int32>> moved = InventoryLayout::moveInBag(items, 10, 2);
	TestTrue(TEXT("Every bag item gets a place"), (moved.Num() == 3));
	TestTrue(TEXT("In their new order"), (moved.Num() == 3) && (moved[0].Key == 20) && (moved[1].Key == 30) && (moved[2].Key == 10));
	TestTrue(TEXT("Numbered from zero"), (moved.Num() == 3) && (moved[0].Value == 0) && (moved[2].Value == 2));

	const TArray<TPair<uint32, int32>> past = InventoryLayout::moveInBag(items, 20, 50);
	TestTrue(TEXT("A place past the end puts it last"), (past.Num() == 3) && (past[2].Key == 20));
	TestEqual(TEXT("A worn item does not move in the bag"), InventoryLayout::moveInBag(items, 40, 0).Num(), 0);

	// Each paperdoll place has one slot and a mask that takes off only what is on it.
	TSet<int32> places;
	for (const InventoryLayout::FPaperdollSlot& slot : InventoryLayout::paperdollSlots())
	{
		places.Add(slot.location);
	}

	TestEqual(TEXT("Thirteen slots, each its own place"), places.Num(), 13);
	TestEqual(TEXT("The right finger alone"), InventoryLayout::unequipMask(13), 0x10);
	TestEqual(TEXT("The left ear alone"), InventoryLayout::unequipMask(9), 0x04);
	TestEqual(TEXT("A place the screen has no slot for"), InventoryLayout::unequipMask(24), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryEquipmentAlignmentTest, "Alverion.UI.InventoryEquipmentAlignment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInventoryEquipmentAlignmentTest::RunTest(const FString& parameters)
{
	UInventoryWidget* inventory = NewObject<UInventoryWidget>();
	inventory->Initialize();
	const TSharedRef<SWidget> widget = inventory->TakeWidget();
	widget->SlatePrepass(1.0f);

	UCharacterPortraitImage* portrait = nullptr;
	inventory->WidgetTree->ForEachWidget([&portrait](UWidget* child)
	{
		if (UCharacterPortraitImage* image = Cast<UCharacterPortraitImage>(child))
		{
			portrait = image;
		}
	});
	if (!TestNotNull(TEXT("The character sheet contains a portrait"), portrait))
	{
		return false;
	}

	const UPanelWidget* frame = portrait->GetParent();
	const UHorizontalBox* equipment = (frame != nullptr) ? Cast<UHorizontalBox>(frame->GetParent()) : nullptr;
	if (!TestNotNull(TEXT("The portrait is in the equipment row"), equipment))
	{
		return false;
	}

	const UVerticalBoxSlot* equipmentSlot = CastChecked<UVerticalBoxSlot>(equipment->Slot);
	TestEqual(TEXT("The equipment row is centred in the character sheet"), equipmentSlot->GetHorizontalAlignment(), HAlign_Center);
	if (TestEqual(TEXT("The portrait has a column of equipment on each side"), equipment->GetChildrenCount(), 3))
	{
		const UHorizontalBoxSlot* left = CastChecked<UHorizontalBoxSlot>(equipment->GetChildAt(0)->Slot);
		const UHorizontalBoxSlot* right = CastChecked<UHorizontalBoxSlot>(equipment->GetChildAt(2)->Slot);
		TestEqual(TEXT("The left slots are centred beside the portrait"), left->GetVerticalAlignment(), VAlign_Center);
		TestEqual(TEXT("The right slots are centred beside the portrait"), right->GetVerticalAlignment(), VAlign_Center);
	}

	return true;
}

#endif
