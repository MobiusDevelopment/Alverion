#pragma once

#include "GameServer/GameServerProtocol.h"

namespace Network
{
namespace GameServer
{
/**
 * One shortcut as ShortcutInit and ShortcutRegister write it: the type, the slot, then fields that depend on the type.
 * Both packets give an item the same twenty bytes after its id and a skill its level, a byte and an int, so one reader serves both.
 */
inline bool readShortcut(FReadBuffer& buffer, FShortcut& shortcut)
{
	int32 type = 0;
	buffer.readInt32(type);
	buffer.readInt32(shortcut.slot);
	buffer.readInt32(shortcut.id);
	shortcut.type = static_cast<EShortcutType>(type);

	if (shortcut.type == EShortcutType::Item)
	{
		// The character type, the shared reuse group, two unknowns and the augmentation.
		buffer.skip(20);
	}
	else if (shortcut.type == EShortcutType::Skill)
	{
		buffer.readInt32(shortcut.level);

		// A byte the server leaves zero, then the character type.
		buffer.skip(1 + 4);
	}
	else
	{
		// The character type.
		buffer.skip(4);
	}

	return !buffer.hasFailed();
}

/** ShortcutInit: every shortcut the character has, sent on entering the world. */
inline bool readShortcutInit(FReadBuffer& buffer, TArray<FShortcut>& shortcuts)
{
	int32 count = 0;
	buffer.readInt32(count);
	if (buffer.hasFailed() || (count < 0))
	{
		return false;
	}

	shortcuts.Reset(count);
	for (int32 index = 0; index < count; index++)
	{
		FShortcut shortcut;
		if (!readShortcut(buffer, shortcut))
		{
			return false;
		}

		shortcuts.Add(shortcut);
	}

	return true;
}
}
}
