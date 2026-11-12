# Alverion server patch for L2J Mobius

The Alverion client builds MetaHuman characters, and the server keeps only a face, a hair style
and a hair colour per character. This patch lets the server store the client's own appearance
block, 24 bytes today, and hand it back, without changing anything a client that sends no block
sees. It also adds Alverion's own items and starts new characters in them.

## What it changes

- `CharacterCreate` reads an optional block after the stock fields: the int `0x31564C41` (the bytes
  `ALV1`), a length byte, then the block. A client that sends nothing after the face is unaffected.
- The block is stored in its own table, `character_appearance`, keyed by character id, and cached.
- `CharSelectionInfo` and `CharInfo` append a trailer at the very end of the packet: every block,
  padded to the longest, then that length and the count as shorts, then the magic. Nothing is
  appended when no listed character has a block, so stock characters produce stock packets.
- Deleting a character deletes its block.
- `Player.ini` turns `DisableCp` on, so all damage goes straight to HP: the client shows HP and MP only.
- `30000-30099.xml` adds Alverion's items, from id 30000 up: past every stock id, below the custom
  wyvern at 50000 and under the 65535 the item schema allows. They sit with the stock items, so
  they load with `CustomItemsLoad` off. Today they are five villager clothes, shirt, pants, gloves,
  boots and hat, and a wooden sword. The client names, describes and dresses them by id.
- `initialEquipment.xml` starts a new human fighter, the class the client creates, with Alverion's
  six items worn and none of the stock ones. It does so with a second human fighter block at the
  end of the file, which replaces the stock one as the later of the two, since the two releases'
  stock blocks differ and no edit of them fits both.
- `initialShortcuts.xml` drops the global shortcuts, the only ones a human fighter gets, so the
  action bar starts empty.

Four upstream Java files change by 26 added lines in total, `Player.ini` changes one value,
`initialEquipment.xml` gains one block, `initialShortcuts.xml` loses one, and one class, one SQL
file and one item file are added:

| File | Change |
| --- | --- |
| `java/.../network/clientpackets/CharacterCreate.java` | read the block, store it after creation |
| `java/.../network/serverpackets/CharSelectionInfo.java` | trailer with one block per character |
| `java/.../network/serverpackets/CharInfo.java` | trailer with the player's block |
| `java/.../network/GameClient.java` | delete the block with the character |
| `dist/game/config/Player.ini` | `DisableCp = True` |
| `java/.../data/sql/CharacterAppearanceTable.java` | new: storage, cache, trailer writer |
| `dist/db_installer/sql/game/character_appearance.sql` | new: the table |
| `dist/game/data/stats/players/initialEquipment.xml` | class 0 starts with Alverion's items alone |
| `dist/game/data/stats/players/initialShortcuts.xml` | no global shortcuts |
| `dist/game/data/stats/items/30000-30099.xml` | new: Alverion's items |

## Hardware information

Nothing to patch. The client sends the stock `RequestHardWareInfo` right after `EnterWorld`, so
`EnableHardwareInfo`, `KickMissingHWID` and `MaxPlayersPerHWID` in `Server.ini` work as they do
with the stock client. Its first field, the one HWID bans and limits key on, is a hash of the
machine id instead of a MAC address, so one machine always sends the same value.

## Applying it

The same patch applies to a standalone High Five server project and to the High Five folder of the
L2J Mobius repository; `<Patch>` below is the path to `alverion-appearance.patch`. From the root
of a standalone project:

```
git apply -p0 --check <Patch>
git apply -p0 <Patch>
```

From the root of the L2J Mobius repository, which keeps every chronicle in one tree:

```
git apply -p0 --check --directory=L2J_Mobius_CT_2.6_HighFive <Patch>
git apply -p0 --directory=L2J_Mobius_CT_2.6_HighFive <Patch>
```

Then rebuild the server and run `dist/db_installer/sql/game/character_appearance.sql` against the
game database (the database installer picks it up with the other game tables).

## Running it without applying it

The patched classes can be compiled on their own and put ahead of the stock ones, which is how it
is tested here without touching the server. Copy the four upstream files into an empty folder,
`<Folder>`, at their `java/...` paths and run `git apply -p0 --include="java/*"` on the patch
there. The data files are read from the server's own `dist` only, so apply the rest there with
`git apply -p0 --include="dist/*"`, or leave out `Player.ini` too if CP should stay on. Then
compile from `<Folder>` and start the server with its classes first:

```
javac -encoding UTF-8 -nowarn -proc:none -cp "<Server>/bin;<Server>/dist/libs/*" -d classes <the five .java files>
java -cp "<Folder>/classes;<Server>/bin;<Server>/dist/libs/*" org.l2jmobius.gameserver.GameServer
```

The second command runs from `<Server>/dist/game`, with the JVM options in its `java.cfg`.

## Layout of this repository

No server files are kept here, only the patch, which carries the new class, the SQL file and the
item file too. It is the one copy of the change and is edited directly. It adds no imports and
names the new class in full instead, and its context lines are ones both kinds of High Five
project share, so one patch fits either.
