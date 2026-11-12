# Items

`Items.csv` holds one row per item id, the same id the server uses. Every race wears the same item: the row names what the item is, and the client fits it to whoever wears it.

| Column | What |
| --- | --- |
| `Id` | The item id, which must not clash with an id the server already uses and cannot pass 65535, the most the server's item schema allows. Alverion's own items start at 30000. |
| `Slot` | Where it is worn: `head`, `chest`, `back`, `main_hand`, `hands`, `legs`, `feet`, `off_hand`, `ear`, `neck` or `finger`. Empty for items that are not worn. |
| `Kind` | `weapon`, `armor`, `jewelry`, `consumable`, `material`, `quest` or `currency`. |
| `Icon` | The texture shown in the bag, as an asset path. Empty for a plain tile. |
| `Visual` | The garment family it shows as on the body, such as `Villager.Shirt`. The client picks the mesh fitted to the wearer's race, sex and face from that family. Empty for items that do not show, such as jewelry. |
| `Stackable` | `true` for items counted in one stack, `false` otherwise. |

The server decides what an item does, where it goes and what it weighs. This file is only what the client needs to show it.

Names and descriptions are in `Content/Localization/StringTables/ST_Items.csv`, keyed `<id>.name` and `<id>.description`, and are translated in each culture's `Content/Localization/Items/<culture>/Items.po`.
