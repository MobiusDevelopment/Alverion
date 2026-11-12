#pragma once

#include "Appearance.h"
#include "CoreMinimal.h"
#include "GameServer/GameServerPackets.h"
#include "LoginServer/LoginServerPackets.h"
#include "Session.h"

namespace Network
{
/**
 * ProtocolVersion, KeyPacket, AuthLogin, then the character list, which creating and deleting both answer with again.
 * Selected means CharSelected has arrived. EnterWorld waits for enterWorld(), so nothing the world sends arrives before there is a world to put it in.
 */
enum class EGameServerStep : uint8
{
	Idle,
	Connecting,
	Authenticating,
	CharacterSelect,
	CreateSent,
	DeleteSent,
	RestoreSent,
	SelectSent,
	Selected,
	InWorld,

	/** RequestRestart sent: world packets stop until the server answers. A refusal goes back to InWorld, consent ends at CharacterSelect. */
	Restarting,
	Failed
};

DECLARE_DELEGATE_OneParam(FGameServerStepChanged, EGameServerStep);
DECLARE_DELEGATE_OneParam(FGameServerFailed, const FString&);
DECLARE_DELEGATE_OneParam(FCharacterCreateRejected, ECreateRejectReason);
DECLARE_DELEGATE(FCharacterCreated);
DECLARE_DELEGATE(FCharacterDeleted);
DECLARE_DELEGATE(FCharacterDeleteRejected);
DECLARE_DELEGATE(FRestartRefused);

/** The game server leg up to a chosen character. Its session must be opened with an FGameCipher, which this flow keys from KeyPacket. */
class NETWORK_API FGameServerFlow
{
public:
	~FGameServerFlow();

	void begin(FSession& session, const FLoginToken& token);
	void tick();

	bool createCharacter(const FString& name, const FAppearance& appearance);
	bool deleteCharacter(uint32 objectId);

	/** Takes back a deletion the server is still waiting out. The fresh list it answers with ends the step. */
	bool restoreCharacter(uint32 objectId);
	bool selectCharacter(uint32 objectId);

	/** Sends EnterWorld. Every packet after it goes to onWorldPacket. */
	bool enterWorld();

	/** Asks to leave the world for the character list, keeping the link. The step changes to CharacterSelect when the server agrees. */
	bool returnToCharacterSelect();

	void end();

	EGameServerStep step() const;
	const TArray<FCharacterListEntry>& characters() const;
	const FCharacterSelected& selected() const;
	const FString& lastError() const;
	FSession* session() const;

	FPacketHandler onWorldPacket;
	FGameServerStepChanged onStepChanged;
	FGameServerFailed onFailed;
	FCharacterCreateRejected onCreateRejected;
	FCharacterCreated onCreated;
	FCharacterDeleteRejected onDeleteRejected;
	FCharacterDeleted onDeleted;

	/** The server kept the character in the world, as it does in combat. */
	FRestartRefused onRestartRefused;

private:
	void handlePacket(uint32 opcode, TArrayView<const uint8> body);
	void send(const FWriteBuffer& buffer);
	void setStep(EGameServerStep next);
	void fail(const FString& reason);
	const FCharacterListEntry* findCharacter(uint32 objectId) const;

	FSession* _session = nullptr;
	FLoginToken _token;
	TArray<FCharacterListEntry> _characters;
	FCharacterSelected _selected;
	FString _lastError;
	EGameServerStep _step = EGameServerStep::Idle;
	bool _versionSent = false;
};
}
