#include "Items/ItemCatalog.h"

#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/LocalizedText.h"

namespace ItemCatalog
{
	namespace
	{
		const TCHAR* const itemCatalogTableId = TEXT("ST_Items");
		const TCHAR* const itemCatalogDataPath = TEXT("Items/Items.csv");

		/** An entry of the item table, or empty when it has none. Asked for this way because a missing key comes back as the key itself. */
		FText itemCatalogText(const FString& key)
		{
			const FStringTableConstPtr table = FStringTableRegistry::Get().FindStringTable(FName(itemCatalogTableId));
			if (!table.IsValid() || !table->FindEntry(key).IsValid())
			{
				return FText::GetEmpty();
			}

			return FText::FromStringTable(FName(itemCatalogTableId), key, EStringTableLoadingPolicy::FindOrFullyLoad);
		}

		/** One cell of a row, trimmed, or empty past the row's end. */
		FString itemCatalogCell(const TArray<FString>& cells, int32 column)
		{
			return cells.IsValidIndex(column) ? cells[column].TrimStartAndEnd() : FString();
		}

		/** Every row of Items.csv, read the first time an item is looked up. */
		const TMap<int32, FItemData>& itemCatalogRows()
		{
			static TMap<int32, FItemData> rows;
			static bool loaded = false;
			if (!loaded)
			{
				loaded = true;
				FString csv;
				if (FFileHelper::LoadFileToString(csv, *(FPaths::ProjectContentDir() / itemCatalogDataPath)))
				{
					for (const FItemData& item : parse(csv))
					{
						rows.Add(item.id, item);
					}
				}
			}

			return rows;
		}
	}

	void registerTable()
	{
		// A CSV rather than a string table asset, as the menus' is, and gathered into a target of its own so item text is translated apart from the menus.
		LOCTABLE_FROMFILE_GAME("ST_Items", "ST_Items", "Localization/StringTables/ST_Items.csv");
	}

	const FItemData* find(int32 itemId)
	{
		return itemCatalogRows().Find(itemId);
	}

	FText name(int32 itemId)
	{
		const FText text = itemCatalogText(FString::Printf(TEXT("%d.name"), itemId));

		// The id as written, since grouping its digits like a quantity would show a name nobody can look up.
		return text.IsEmpty() ? FText::Format(LocalizedText::get(TEXT("inventory.item_unknown")), FText::AsCultureInvariant(FString::FromInt(itemId))) : text;
	}

	FText description(int32 itemId)
	{
		return itemCatalogText(FString::Printf(TEXT("%d.description"), itemId));
	}

	FText slotName(int32 itemId)
	{
		const FItemData* item = find(itemId);
		return ((item != nullptr) && !item->slot.IsEmpty()) ? LocalizedText::get(TEXT("inventory.slot_") + item->slot) : FText::GetEmpty();
	}

	TArray<FItemData> parse(const FString& csv)
	{
		TArray<FItemData> items;
		TArray<FString> lines;
		csv.ParseIntoArrayLines(lines);
		if (lines.IsEmpty())
		{
			return items;
		}

		TArray<FString> header;
		lines[0].ParseIntoArray(header, TEXT(","), false);
		for (FString& column : header)
		{
			column.TrimStartAndEndInline();
		}

		const int32 idColumn = header.IndexOfByKey(TEXT("Id"));
		const int32 slotColumn = header.IndexOfByKey(TEXT("Slot"));
		const int32 kindColumn = header.IndexOfByKey(TEXT("Kind"));
		const int32 iconColumn = header.IndexOfByKey(TEXT("Icon"));
		const int32 visualColumn = header.IndexOfByKey(TEXT("Visual"));
		const int32 stackableColumn = header.IndexOfByKey(TEXT("Stackable"));
		for (int32 index = 1; index < lines.Num(); index++)
		{
			TArray<FString> cells;
			lines[index].ParseIntoArray(cells, TEXT(","), false);
			const FString id = itemCatalogCell(cells, idColumn);
			if (id.IsEmpty() || !id.IsNumeric())
			{
				continue;
			}

			FItemData item;
			item.id = FCString::Atoi(*id);
			item.slot = itemCatalogCell(cells, slotColumn);
			item.kind = itemCatalogCell(cells, kindColumn);
			item.icon = itemCatalogCell(cells, iconColumn);
			item.visual = itemCatalogCell(cells, visualColumn);
			item.stackable = itemCatalogCell(cells, stackableColumn).Equals(TEXT("true"), ESearchCase::IgnoreCase);
			items.Add(item);
		}

		return items;
	}
}
