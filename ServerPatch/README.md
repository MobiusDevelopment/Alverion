# Alverion appearance extension for L2J Mobius

The Alverion client builds MetaHuman characters, and the server keeps only a face, a hair style
and a hair colour per character. This patch lets the server store the client's own appearance
block, 24 bytes today, and hand it back, without changing anything a client that sends no block
sees.

## What it changes

- `CharacterCreate` reads an optional block after the stock fields: the int `0x31564C41` (the bytes
  `ALV1`), a length byte, then the block. A client that sends nothing after the face is unaffected.
- The block is stored in its own table, `character_appearance`, keyed by character id, and cached.
- `CharSelectionInfo` and `CharInfo` append a trailer at the very end of the packet: every block,
  padded to the longest, then that length and the count as shorts, then the magic. Nothing is
  appended when no listed character has a block, so stock characters produce stock packets.
- Deleting a character deletes its block.
- `Player.ini` turns `DisableCp` on, so all damage goes straight to HP: the client shows HP and MP only.

Four upstream Java files change by 26 added lines in total, `Player.ini` changes one value, and one
class and one SQL file are added:

| File | Change |
| --- | --- |
| `java/.../network/clientpackets/CharacterCreate.java` | read the block, store it after creation |
| `java/.../network/serverpackets/CharSelectionInfo.java` | trailer with one block per character |
| `java/.../network/serverpackets/CharInfo.java` | trailer with the player's block |
| `java/.../network/GameClient.java` | delete the block with the character |
| `dist/game/config/Player.ini` | `DisableCp = True` |
| `java/.../data/sql/CharacterAppearanceTable.java` | new: storage, cache, trailer writer |
| `dist/db_installer/sql/game/character_appearance.sql` | new: the table |

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
`<Folder>`, at their `java/...` paths and run `git apply -p0 --exclude=dist/game/config/Player.ini`
on the patch there; set `DisableCp = True` in the server's own `Player.ini` if CP should be off.
Then compile from `<Folder>` and start the server with its classes first:

```
javac -encoding UTF-8 -nowarn -proc:none -cp "<Server>/bin;<Server>/dist/libs/*" -d classes <the five .java files>
java -cp "<Folder>/classes;<Server>/bin;<Server>/dist/libs/*" org.l2jmobius.gameserver.GameServer
```

The second command runs from `<Server>/dist/game`, with the JVM options in its `java.cfg`.

## Layout of this repository

No server files are kept here, only the patch, which carries the new class and the SQL file
too. It is the one copy of the change and is edited directly. It adds no imports and names the
new class in full instead, and its context lines are ones both kinds of High Five project share,
so one patch fits either.
