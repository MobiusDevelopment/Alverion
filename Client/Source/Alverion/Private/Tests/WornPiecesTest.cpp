#if WITH_DEV_AUTOMATION_TESTS

#include "Appearance/AppearanceApplier.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWornPiecesTest, "Alverion.Characters.WornPieces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWornPiecesTest::RunTest(const FString& parameters)
{
	// The server's starting equipment, as Items.csv lists it, shows as the whole starter outfit and the sword.
	const TArray<int32> starter = { 30001, 30002, 30003, 30004, 30005, 30006 };
	TestEqual(TEXT("The starting equipment is the whole outfit"), Appearance::wornPieces(starter), Appearance::wornEverything);

	const TArray<int32> swordOnly = { 0, 30006, 0 };
	TestEqual(TEXT("The sword alone, with empty slots around it"), Appearance::wornPieces(swordOnly), Appearance::wornSword);

	const TArray<int32> shirtAndPants = { 30002, 30001 };
	TestEqual(TEXT("Order does not matter"), Appearance::wornPieces(shirtAndPants), static_cast<uint8>(Appearance::wornShirt | Appearance::wornPants));
	const TArray<int32> sandalsOnly = { 30004 };
	TestEqual(TEXT("The renamed sandals retain their equipment bit"), Appearance::wornPieces(sandalsOnly), Appearance::wornSandals);

	const TArray<int32> unknown = { 987654 };
	TestEqual(TEXT("An item the data does not list shows nothing"), Appearance::wornPieces(unknown), static_cast<uint8>(0));
	TestEqual(TEXT("Nothing worn is the underwear alone"), Appearance::wornPieces(TConstArrayView<int32>()), static_cast<uint8>(0));
	return true;
}

#endif
