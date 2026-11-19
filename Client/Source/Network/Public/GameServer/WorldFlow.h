#pragma once

#include "CoreMinimal.h"
#include "GameServer/GameServerFlow.h"
#include "GameServer/GameServerPackets.h"

namespace Network
{
/** Where somebody else is headed or has stopped. Destinations are walked to at the mover's speed; stops are where it now stands. */
enum class EMoveKind : uint8
{
	Destination,
	Stopped
};

DECLARE_DELEGATE_OneParam(FCharacterSpawned, const FCharacterSpawn&);
DECLARE_DELEGATE_OneParam(FCharacterDespawned, uint32);
DECLARE_DELEGATE_TwoParams(FCharacterMoved, const FObjectPlace&, EMoveKind);
DECLARE_DELEGATE_TwoParams(FLocalCorrected, const FObjectPlace&, bool);
DECLARE_DELEGATE_OneParam(FChatReceived, const FChatMessage&);
DECLARE_DELEGATE_OneParam(FNpcHtmlReceived, const FNpcHtml&);
DECLARE_DELEGATE_TwoParams(FMoveTypeChanged, uint32, bool);
DECLARE_DELEGATE_OneParam(FAttackReceived, const FAttack&);
DECLARE_DELEGATE_TwoParams(FLifeChanged, uint32, bool);
DECLARE_DELEGATE_TwoParams(FCombatStanceChanged, uint32, bool);
DECLARE_DELEGATE_OneParam(FCorpseLootReceived, const FCorpseLoot&);

/**
 * The world, from EnterWorld on.
 * The local character moves itself and reports in keyboard mode, which makes the server take its positions as true.
 * Everybody else arrives as spawns, destinations, stops and corrections.
 */
class NETWORK_API FWorldFlow
{
public:
	~FWorldFlow();

	/** Sends EnterWorld. Called once the world exists, because everything the server says after it needs somewhere to land. */
	bool begin(FGameServerFlow& gameServer);
	void end();

	void setLocalState(const FVector& location, float yaw, bool wantsToMove);
	void tick(float deltaSeconds);

	bool isActive() const;

	/** The latest UserInfo, all zero until the first one arrives after EnterWorld. */
	const FUserInfo& userInfo() const;

	/** Everything the character carries, worn or in the bag, as the server last listed and updated it. */
	const TArray<FInventoryItem>& items() const;

	/** Every shortcut the character has, keyed by slot. */
	const TMap<int32, FShortcut>& shortcuts() const;

	/** Counts up whenever the items or the shortcuts change, so a screen can tell when it has something new to draw. */
	uint32 revision() const;

	/** Uses an item: one that is worn comes off, one that can be worn goes on, and anything else is used. */
	bool useItem(uint32 objectId);

	/** Takes off what is worn on one body part, named by its mask. */
	bool unequip(int32 bodyPartMask);

	/** Puts a shortcut in its slot. The server confirms it, and the copy here changes at once so the bar does not wait. */
	bool setShortcut(const FShortcut& shortcut);

	/** Empties a shortcut slot, here and on the server, which sends nothing back. */
	bool clearShortcut(int32 slot);

	/** Saves the bag's order as pairs of object id and place, here at once and on the server. */
	bool saveInventoryOrder(TConstArrayView<TPair<uint32, int32>> order);

	/** Asks the server to destroy this many of an item. It answers with an InventoryUpdate, or keeps an item that cannot be destroyed. */
	bool destroyItem(uint32 objectId, int64 count);

	FCharacterSpawned onSpawned;
	FCharacterDespawned onDespawned;
	FCharacterMoved onMoved;

	/** The server moving this client's own character. True for a teleport, which snaps rather than blends. */
	FLocalCorrected onCorrected;

	/** Somebody started running or walking. This client's own character is not told of, since it chooses for itself. */
	FMoveTypeChanged onMoveTypeChanged;

	/** A line of chat heard, this client's own included, since the server says it back to the speaker too. */
	FChatReceived onChat;

	/** A dialog page, whether it answers talking to a townsperson or a link in the dialog already open. */
	FNpcHtmlReceived onNpcHtml;

	/** Somebody struck, this client's own character included: the attacker and every blow, which lands as the swing does. */
	FAttackReceived onAttack;

	/** Somebody fell, false, or got up again, true. */
	FLifeChanged onLifeChanged;

	/** Somebody went into combat, true, or left it, false, which is when a fighter holds a weapon ready. */
	FCombatStanceChanged onCombatStance;

	/**
	 * Fights a creature: the second click on a monster the server has selected is an attack, the way it is talking for a townsperson.
	 * The server keeps striking until the creature falls or this character moves, and moves nobody into reach, since this client steers its own.
	 */
	bool attack(uint32 objectId);

	/** Asks for a fallen character to be taken to the nearest village and revived there. */
	bool returnToVillage();

	/** What a corpse holds, each time the server answers a loot request. */
	FCorpseLootReceived onCorpseLoot;

	/** Asks what a corpse holds. Anyone standing next to it may loot it, and the server answers with its loot list. */
	bool openLoot(uint32 corpseId);

	/** Takes one stack off a corpse, named by its place in the list the server last sent and its item. */
	bool takeLoot(uint32 corpseId, int32 index, int32 itemId);

	/** Takes everything off a corpse that fits in the bag. */
	bool takeAllLoot(uint32 corpseId);

	/** What the server says this client has selected. Zero for nothing. */
	uint32 targetObjectId() const;

	/** The health the server last gave for a creature, which it gives for whatever this client has selected and fights. False when it gave none. */
	bool healthOf(uint32 objectId, int32& current, int32& maximum) const;

	/** Says a line on a channel. Empty lines are not sent, and nothing goes out outside the world. */
	bool say(const FString& text, EChatChannel channel);

	/**
	 * Opens the dialog of a townsperson in one go.
	 * The server selects an object on the first click and deals with it on the second, so this clicks once when something else is selected, and clicks again when the server confirms the selection.
	 * The second click waits out the server's flood protection, which drops clicks that come too close together.
	 * Nothing goes out outside the world, and a selection the server never confirms is given up on.
	 */
	bool talkTo(uint32 objectId);

	/** Sends the command of a link or button in the dialog on screen. */
	bool bypass(const FString& command);

	/** Asks for the page a link in the dialog on screen names. */
	bool link(const FString& file);

private:
	void handlePacket(uint32 opcode, TArrayView<const uint8> body);
	void sendMove(const FVector& target);
	void sendValidate();
	void sendAction(uint32 objectId);

	/** Sends one loot request for a corpse. Returns false when there is no world to send to or no corpse. */
	bool sendLoot(GameServer::ELootAction action, uint32 corpseId, int32 index, int32 itemId);

	/** Whether there is a world to send to. */
	bool canSend() const;

	FGameServerFlow* _gameServer = nullptr;
	uint32 _localObjectId = 0;
	FUserInfo _userInfo;
	TArray<FInventoryItem> _items;
	TMap<int32, FShortcut> _shortcuts;
	uint32 _revision = 0;

	FVector _location = FVector::ZeroVector;
	FVector _previousLocation = FVector::ZeroVector;
	FVector _moveDirection = FVector::ZeroVector;
	float _yaw = 0.0f;
	bool _wantsToMove = false;
	bool _hasLocalState = false;
	bool _moving = false;

	float _sinceMoveSent = 0.0f;
	float _sinceValidateSent = 0.0f;

	/** What the server says this client has selected, from MyTargetSelected and TargetUnselected. Zero for nothing. */
	uint32 _targetObjectId = 0;

	/** Current and maximum health of the creatures other than this client's own, from their StatusUpdates. */
	TMap<uint32, TPair<int32, int32>> _health;

	/** The townsperson being talked to, until the second click goes out or the wait runs out. Zero when there is none. */
	uint32 _talkObjectId = 0;
	bool _talkSelected = false;
	float _talkSeconds = 0.0f;
	float _sinceActionSent = 0.0f;
};
}
