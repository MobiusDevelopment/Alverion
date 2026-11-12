#pragma once

#include "Appearance.h"
#include "CoreMinimal.h"
#include "Core/ReadBuffer.h"
#include "Core/WriteBuffer.h"

/**
 * The game server's packets, as L2J Mobius writes and reads them, plus the appearance extension in ServerPatch.
 * Reads stop at the last field this client uses. Positions and headings become world centimetres and degrees here, so nothing past this header sees a server unit.
 */
namespace Network
{
/**
 * How long one server world unit is here.
 * A human's collision height is 23 units and it is a half height, so a 46 unit body against a 180cm MetaHuman puts a unit near 4cm.
 * Run speeds agree at that scale: 115 units a second is a 4.6m/s run.
 */
inline constexpr double worldUnitCm = 4.0;

inline double toCentimetres(int32 units)
{
	return static_cast<double>(units) * worldUnitCm;
}

inline int32 toUnits(double centimetres)
{
	return FMath::RoundToInt32(centimetres / worldUnitCm);
}

/** The sea surface around the starting island, rounded. The client puts it at Z zero, so heights are measured from the water. */
inline constexpr int32 seaLevelUnits = -3800;

inline FVector toLocation(int32 x, int32 y, int32 z)
{
	return FVector(toCentimetres(x), toCentimetres(y), toCentimetres(z - seaLevelUnits));
}

inline int32 toHeightUnits(double centimetres)
{
	return toUnits(centimetres) + seaLevelUnits;
}

/** Server headings run 0 to 65535 for a full turn, in the same direction as yaw. */
inline float headingToYaw(int32 heading)
{
	return static_cast<float>(heading & 0xFFFF) * (360.0f / 65536.0f);
}

inline int32 yawToHeading(float yaw)
{
	const float wrapped = FMath::Fmod(FMath::Fmod(yaw, 360.0f) + 360.0f, 360.0f);
	return FMath::RoundToInt32(wrapped * (65536.0f / 360.0f)) & 0xFFFF;
}

enum class ERace : int32
{
	Human = 0,
	Elf = 1,
	DarkElf = 2,
	Orc = 3,
	Dwarf = 4,
	Kamael = 5
};

/** CharCreateFail's codes. */
enum class ECreateRejectReason : int32
{
	CreationFailed = 0,
	TooManyCharacters = 1,
	NameTaken = 2,
	NameTooLong = 3,
	NameInvalid = 4,
	CreationNotAllowed = 5,
	ChooseAnotherServer = 6
};

inline const TCHAR* createRejectReasonName(ECreateRejectReason reason)
{
	switch (reason)
	{
		case ECreateRejectReason::TooManyCharacters:
		{
			return TEXT("This account cannot hold another character.");
		}
		case ECreateRejectReason::NameTaken:
		{
			return TEXT("That name is taken.");
		}
		case ECreateRejectReason::NameTooLong:
		{
			return TEXT("That name is too long.");
		}
		case ECreateRejectReason::NameInvalid:
		{
			return TEXT("Names are 2 to 16 letters and digits.");
		}
		default:
		{
			return TEXT("The character could not be created.");
		}
	}
}

struct NETWORK_API FCharacterListEntry
{
	uint32 objectId = 0;

	/** The server picks characters by their place in the list, not by id. */
	int32 slot = 0;

	FString name;
	int32 level = 0;
	bool female = false;
	int32 race = 0;
	int32 classId = 0;
	int32 hairStyle = 0;
	int32 hairColor = 0;
	int32 face = 0;

	/** Seconds until a character marked for deletion goes, zero for one that is not. */
	int32 deleteSeconds = 0;

	FVector location = FVector::ZeroVector;

	/** The extension's bytes, empty for a character made without them. */
	TArray<uint8> appearance;

	/** The item id worn in each paperdoll slot, zero where nothing is. */
	TArray<int32> worn;
};

struct NETWORK_API FCharacterSelected
{
	uint32 objectId = 0;
	FString name;
	FString title;
	int32 level = 0;
	bool female = false;
	int32 race = 0;
	int32 classId = 0;
	FVector location = FVector::ZeroVector;
	int32 currentHp = 0;
};

/** The chat channels the client speaks on, numbered as the server numbers them. The server sends others too, such as announcements. */
enum class EChatChannel : int32
{
	General = 0,
	Shout = 1
};

/** One line of chat as the server sends it. */
struct NETWORK_API FChatMessage
{
	uint32 senderObjectId = 0;

	/** The server's channel number: an EChatChannel, or one of the channels the client only listens to. */
	int32 channel = 0;

	FString senderName;
	FString text;
};

/** This client's own character as UserInfo describes it: who it is and the figures the character sheet shows. */
struct NETWORK_API FUserInfo
{
	uint32 objectId = 0;
	int32 level = 0;

	int32 strength = 0;
	int32 dexterity = 0;
	int32 constitution = 0;
	int32 intelligence = 0;
	int32 wit = 0;
	int32 spirit = 0;

	int32 maxHp = 0;
	int32 currentHp = 0;
	int32 maxMp = 0;
	int32 currentMp = 0;

	int32 attack = 0;
	int32 defense = 0;
	int32 magicAttack = 0;
	int32 magicDefense = 0;
	int32 accuracy = 0;
	int32 evasion = 0;
	int32 critical = 0;

	/** In the server's units, as the server states it. */
	int32 runSpeed = 0;

	/** How many items the bag holds. Zero until a UserInfo that reaches that far arrives. */
	int32 inventoryLimit = 0;
};

/** One item this client's character carries, in the bag or worn, as ItemList and InventoryUpdate describe it. */
struct NETWORK_API FInventoryItem
{
	uint32 objectId = 0;
	int32 itemId = 0;

	/** Where it sits: its place in the bag, or the paperdoll slot it is worn in. */
	int32 location = 0;
	int64 count = 0;

	/** The server's broad kind: 0 weapon, 1 armor or shield, 2 jewelry, 3 quest item, 4 adena, 5 anything else. */
	uint16 type2 = 0;
	bool equipped = false;

	/** The body part mask of the slots it is worn on. */
	int32 bodyPart = 0;
	uint16 enchant = 0;
};

/** What a shortcut points at, numbered as the server numbers them. */
enum class EShortcutType : int32
{
	None = 0,
	Item = 1,
	Skill = 2,
	Action = 3,
	Macro = 4,
	Recipe = 5,
	Bookmark = 6
};

/** One action bar shortcut. An item shortcut names the item's object id, not the item, so it follows that one item. */
struct NETWORK_API FShortcut
{
	/** The page times twelve plus the place on the page. */
	int32 slot = 0;
	EShortcutType type = EShortcutType::None;
	int32 id = 0;
	int32 level = 0;
};

/** One StatusUpdate: some figures of one creature that changed, each as an attribute id and its new value. */
struct NETWORK_API FStatusUpdate
{
	uint32 objectId = 0;
	TArray<TPair<int32, int32>> attributes;
};

/** Somebody or something coming into view: a player from CharInfo, or a monster or townsperson from NpcInfo. */
struct NETWORK_API FCharacterSpawn
{
	uint32 objectId = 0;
	FString name;
	FString title;
	FVector location = FVector::ZeroVector;
	float yaw = 0.0f;

	/** Centimetres a second, already multiplied by the server's move multiplier. */
	float runSpeed = 0.0f;
	float walkSpeed = 0.0f;
	float swimSpeed = 0.0f;

	/** Whether it runs rather than walks. Players run until they walk, and townspeople and monsters walk until the server has them run. */
	bool running = true;

	bool isNpc = false;
	int32 npcId = 0;
	bool attackable = false;

	bool female = false;
	int32 race = 0;
	int32 hairStyle = 0;
	int32 hairColor = 0;
	int32 face = 0;
	TArray<uint8> appearance;

	/** For a player, the item id shown in each paperdoll slot CharInfo lists, zero where nothing is. Empty for anything else. */
	TArray<int32> worn;
};

/** One dialog page as NpcHtmlMessage carries it: the text is HTML of the kind the server's data files hold. */
struct NETWORK_API FNpcHtml
{
	uint32 npcObjectId = 0;
	FString html;

	/** Zero for a townsperson's dialog; the server's own windows, such as the admin pages, name an item. */
	int32 itemId = 0;
};

/** An object id and a place, which is all MoveToLocation, StopMove, ValidateLocation and TeleportToLocation carry. */
struct NETWORK_API FObjectPlace
{
	uint32 objectId = 0;
	FVector location = FVector::ZeroVector;
	float yaw = 0.0f;
};

namespace GameServer
{
/** The newest protocol revision the server accepts. */
inline constexpr int32 protocolRevision = 273;

/** The paperdoll slots CharSelectionInfo lists, one int each. */
inline constexpr int32 paperdollSlots = 26;

/** The paperdoll slots CharInfo lists, fewer than the character list's: what another player can see on someone, without the ears, neck and fingers. */
inline constexpr int32 charInfoPaperdollSlots = 21;

/** Marks the appearance extension, as the bytes 'A' 'L' 'V' '1'. A server without the patch never sends it and ignores it when sent. */
inline constexpr int32 appearanceMagic = 0x31564C41;

namespace Opcode
{
	// Server to client.
	inline constexpr uint32 deleteObject = 0x08;
	inline constexpr uint32 characterSelectionInfo = 0x09;
	inline constexpr uint32 loginFail = 0x0A;
	inline constexpr uint32 characterSelected = 0x0B;
	inline constexpr uint32 npcInfo = 0x0C;
	inline constexpr uint32 characterCreateSuccess = 0x0F;
	inline constexpr uint32 characterCreateFail = 0x10;
	inline constexpr uint32 itemList = 0x11;
	inline constexpr uint32 statusUpdate = 0x18;
	inline constexpr uint32 npcHtmlMessage = 0x19;
	inline constexpr uint32 characterDeleteSuccess = 0x1D;
	inline constexpr uint32 characterDeleteFail = 0x1E;
	inline constexpr uint32 inventoryUpdate = 0x21;
	inline constexpr uint32 teleportToLocation = 0x22;
	inline constexpr uint32 targetUnselected = 0x24;
	inline constexpr uint32 changeMoveType = 0x28;
	inline constexpr uint32 versionCheck = 0x2E;
	inline constexpr uint32 moveToLocation = 0x2F;
	inline constexpr uint32 charInfo = 0x31;
	inline constexpr uint32 userInfo = 0x32;
	inline constexpr uint32 shortcutRegister = 0x44;
	inline constexpr uint32 shortcutInit = 0x45;
	inline constexpr uint32 stopMove = 0x47;
	inline constexpr uint32 creatureSay = 0x4A;
	inline constexpr uint32 restartResponse = 0x71;
	inline constexpr uint32 validateLocation = 0x79;
	inline constexpr uint32 myTargetSelected = 0xB9;

	// Client to server.
	inline constexpr uint8 logout = 0x00;
	inline constexpr uint8 characterCreate = 0x0C;
	inline constexpr uint8 characterDelete = 0x0D;
	inline constexpr uint8 protocolVersion = 0x0E;
	inline constexpr uint8 moveBackwardToLocation = 0x0F;
	inline constexpr uint8 enterWorld = 0x11;
	inline constexpr uint8 characterSelect = 0x12;
	inline constexpr uint8 requestUnEquipItem = 0x16;
	inline constexpr uint8 useItem = 0x19;
	inline constexpr uint8 action = 0x1F;
	inline constexpr uint8 requestLinkHtml = 0x22;
	inline constexpr uint8 requestBypassToServer = 0x23;
	inline constexpr uint8 authLogin = 0x2B;
	inline constexpr uint8 appearing = 0x3A;
	inline constexpr uint8 requestShortcutReg = 0x3D;
	inline constexpr uint8 requestShortcutDel = 0x3F;
	inline constexpr uint8 say2 = 0x49;
	inline constexpr uint8 requestRestart = 0x57;
	inline constexpr uint8 validatePosition = 0x59;
	inline constexpr uint8 characterRestore = 0x7B;

	// Extended client packets: this byte, then a uint16 naming the packet.
	inline constexpr uint8 extended = 0xD0;
	inline constexpr int16 requestSaveInventoryOrder = 0x24;
	inline constexpr int16 requestGotoLobby = 0x36;
	inline constexpr int16 requestHardWareInfo = 0x96;
}
}
}
