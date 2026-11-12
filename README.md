<p align="center">
  <img src="Images/ConceptArt.png" width="100%" alt="Alverion, a fantasy MMORPG">
</p>

Unreal Engine 5 client for L2J Mobius.

## What it does today

- Logs in through the login server: Blowfish and RSA as the stock client does them.
- Menus for login, server select, character select and creation, options and an in-game menu, in front of a small scene, `L_Login`.
- Lists, creates and deletes characters, and enters the world.
- Sends the machine description right after entering the world, so a server with `KickMissingHWID` on keeps the player. Where the stock client puts its MAC address it sends a hash of the machine id.
- The world is `L_Continent`, a World Partition map with Mesh Terrain. Start Island, its first land, sits under the server's human starting area so its spawns stand on something. Shape it in the editor's Mesh Terrain mode.
- Runs in keyboard mode, where the server takes the client's position as true. Other players, townspeople and monsters walk where the server sends them.
- Players are the MetaHumans their stored appearance describes, in character select and creation and in the world. Until they have models of their own, townspeople wear a default MetaHuman and monsters a goblin MetaHuman. They idle, walk, run and fall.
- E talks to the townsperson in front, and their dialog opens with its links, buttons and entry fields working. Walking away closes it.

## Layout

| Folder | What |
| --- | --- |
| `Client` | The Unreal Engine 5.8 project. `Network` speaks the server protocol, `Alverion` is the game. |
| `ServerPatch` | One patch for `L2J_Mobius` High Five, the SVN project or the free release, that stores each character's appearance block and adds Alverion's items. See its README. |

## MetaHumans

The characters are MetaHumans, about 3 GB, so they are not in the repository. Download them here:

https://drive.google.com/file/d/1JTZIa5qltiwsKLUFXpyVkKNN4TzLhFsZ/view?usp=sharing

Extract the zip at the root of the repository, so that its folders land in `Client/Content/Characters/MetaHumans/` (`Goblin`, `Grooms` and `Unpacked`). Git ignores that folder. Without it the game still runs, with coloured placeholder figures standing in for the characters.

MetaHumans are covered by the Unreal Engine EULA: share the download only with people who have accepted it.

## Equipment

Download the equipment assets here:

https://drive.google.com/file/d/1hhmGeOcVZUqQ2xE4g6my3X9aukO6NApo/view?usp=sharing

Extract the zip so that its folders land in `Client/Content/Equipment/`.

## Animations

The character animations are Quaternius's Universal Animation Library, packs 1 and 2, baked onto the MetaHuman skeleton, with the idle and the run baked again for the goblin's shorter legs. Download them here:

https://drive.google.com/file/d/1OoAfJC1oYUrg2F5ELrx0W54TfZtBvdbW/view?usp=sharing

Extract the zip so that the clips land in `Client/Content/Characters/Animations/UAL/`. The packs are CC0, so no credit is required.

## Running it

1. Run an L2J Mobius login server and game server, with the patch applied and `character_appearance.sql` imported.
2. Download the MetaHumans, equipment and animations from their respective sections.
3. Build `Client/Alverion.uproject` with Unreal Engine 5.8 and run it.
4. Log in. A server with `AutoCreateAccounts` on makes the account the first time.

The client logs in through 127.0.0.1 on port 2106. `-loginserver=host`, `-loginserver=host:port` or `-loginserver=:port` names another login server; the game server address comes from the login server's list.

Development builds also accept `-autologin=account,password,character`.

Everything runs straight from the editor with Play In Editor, the world included. In play, Escape opens the game menu and Shift+Escape stops play. A standalone `-game` run from the editor binary reaches the login screen but not the world, because the terrain plugin needs an editor library to read the map.

```
RunUAT BuildCookRun -project=Client/Alverion.uproject -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory=Client/Saved/Packaged
```

After changing the terrain, compile its sections again before packaging:

```
UnrealEditor-Cmd Client/Alverion.uproject /Game/Maps/L_Continent -run=WorldPartitionBuilderCommandlet -Builder=WorldPartitionMeshPartitionBuilder -SCCProvider=None
```

## Tests

```
UnrealEditor-Cmd Client/Alverion.uproject -ExecCmds="Automation RunTests Alverion;Quit" -nullrhi
```

`Alverion.Network` needs nothing. `Alverion.Live` needs both servers running on this machine.

## Licence

The client is MIT licensed, see [LICENSE](LICENSE).
