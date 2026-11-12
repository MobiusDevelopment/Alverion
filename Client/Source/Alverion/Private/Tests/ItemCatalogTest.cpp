#if WITH_DEV_AUTOMATION_TESTS

#include "Items/ItemCatalog.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemCatalogTest, "Alverion.UI.ItemCatalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FItemCatalogTest::RunTest(const FString& parameters)
{
	// Columns out of their usual order, a row with no visual, a stack, and a line that is not an item.
	const FString csv = TEXT("Kind,Id,Visual,Slot,Stackable,Icon\n")
		TEXT("armor,90001,Villager.Shirt,chest,false,/Game/Items/Icons/T_VillagerShirt\n")
		TEXT("jewelry,90002,,finger,FALSE,\n")
		TEXT("material, 90003 ,,,true,\n")
		TEXT("note,not an id,,,,\n");

	const TArray<FItemData> items = ItemCatalog::parse(csv);
	if (!TestEqual(TEXT("Three items, the note skipped"), items.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("Id read by its column, not its place"), items[0].id, 90001);
	TestEqual(TEXT("Slot"), items[0].slot, FString(TEXT("chest")));
	TestEqual(TEXT("Kind"), items[0].kind, FString(TEXT("armor")));
	TestEqual(TEXT("One garment family for every race"), items[0].visual, FString(TEXT("Villager.Shirt")));
	TestEqual(TEXT("Icon"), items[0].icon, FString(TEXT("/Game/Items/Icons/T_VillagerShirt")));
	TestFalse(TEXT("Not a stack"), items[0].stackable);
	TestTrue(TEXT("Jewelry shows nowhere on the body"), items[1].visual.IsEmpty());
	TestEqual(TEXT("Spaces around a cell are dropped"), items[2].id, 90003);
	TestTrue(TEXT("A stack"), items[2].stackable);
	TestEqual(TEXT("An empty file has no items"), ItemCatalog::parse(FString()).Num(), 0);

	// An item the table does not name still shows as something, numbered.
	TestTrue(TEXT("A stand-in name carries the id"), ItemCatalog::name(987654).ToString().Contains(TEXT("987654")));
	TestTrue(TEXT("And no description"), ItemCatalog::description(987654).IsEmpty());
	return true;
}

#endif
