#pragma once

#include "CoreMinimal.h"

/** What the client knows of an item beyond what the server sends: where it is worn, what it is, its icon and how it shows on the body. */
struct ALVERION_API FItemData
{
	int32 id = 0;

	/** The paperdoll slot it is worn in, by the name its caption is keyed under, or empty for an item that is not worn. */
	FString slot;
	FString kind;

	/** The texture shown in the bag, as an asset path, or empty for a plain tile. */
	FString icon;

	/** The garment family it shows as on the body, the same for every race; the wearer's own fitted mesh is picked from it. */
	FString visual;
	bool stackable = false;
};

/**
 * Items as the client shows them: one row per item id in Content/Items/Items.csv, and their names and descriptions in the ST_Items string table.
 * An item is the same for every race. The server decides what it does; this is only how it is shown.
 */
namespace ItemCatalog
{
	/** Registers the item string table. Called once when the game module starts. */
	ALVERION_API void registerTable();

	/** An item's row, or null for an item the file does not list. */
	ALVERION_API const FItemData* find(int32 itemId);

	/** An item's name, or a numbered stand-in for an item the table does not name yet. */
	ALVERION_API FText name(int32 itemId);

	/** An item's description, or empty for one the table does not describe. */
	ALVERION_API FText description(int32 itemId);

	/** Where an item is worn, in the current language, or empty for one that is not worn or not listed. */
	ALVERION_API FText slotName(int32 itemId);

	/** Reads the rows of an Items.csv by its header, so the columns may come in any order. Rows whose id is not a number are skipped. */
	ALVERION_API TArray<FItemData> parse(const FString& csv);
}
