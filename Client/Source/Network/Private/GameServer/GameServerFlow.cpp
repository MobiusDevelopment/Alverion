#include "GameServer/GameServerFlow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAlverionGameServer, Display, All);

namespace Network
{
namespace
{
	// What the server accepts in its own fields at creation. The appearance block is folded into these, and the rest of it rides in the extension.
	constexpr int32 maleHairStyles = 5;
	constexpr int32 femaleHairStyles = 7;
	constexpr int32 hairColors = 4;
	constexpr int32 faces = 3;

	// The starting class every account may begin as.
	constexpr int32 starterClassId = 0;
}

FGameServerFlow::~FGameServerFlow()
{
	end();
}

void FGameServerFlow::begin(FSession& session, const FLoginToken& token)
{
	_session = &session;
	_token = token;
	_characters.Reset();
	_selected = FCharacterSelected();
	_lastError.Reset();
	_versionSent = false;

	_session->setHandler(FPacketHandler::CreateRaw(this, &FGameServerFlow::handlePacket));
	setStep(EGameServerStep::Connecting);
}

void FGameServerFlow::end()
{
	if (_session != nullptr)
	{
		_session->setHandler(FPacketHandler());
		_session = nullptr;
	}

	_step = EGameServerStep::Idle;
}

void FGameServerFlow::tick()
{
	if ((_session == nullptr) || (_step == EGameServerStep::Idle) || (_step == EGameServerStep::Failed))
	{
		return;
	}

	if (_session->hasFailed())
	{
		fail(_session->lastError());
		return;
	}

	if (_session->state() == ESessionState::Disconnected)
	{
		fail(TEXT("The game server closed the connection."));
		return;
	}

	// The one packet that goes out in the clear, once the socket is open.
	if (!_versionSent && (_session->state() == ESessionState::Connected))
	{
		FWriteBuffer buffer;
		GameServer::writeProtocolVersion(buffer);
		send(buffer);
		_versionSent = true;
	}
}

bool FGameServerFlow::createCharacter(const FString& name, const FAppearance& appearance)
{
	if ((_session == nullptr) || (_step != EGameServerStep::CharacterSelect))
	{
		return false;
	}

	const bool female = !appearance.male;
	const int32 hairStyle = appearance.hairStyle % (female ? femaleHairStyles : maleHairStyles);

	FWriteBuffer buffer;
	GameServer::writeCharacterCreate(buffer, name, female, starterClassId, hairStyle, appearance.hairColor % hairColors, appearance.head % faces, appearance.pack());
	send(buffer);

	setStep(EGameServerStep::CreateSent);
	return true;
}

bool FGameServerFlow::deleteCharacter(uint32 objectId)
{
	const FCharacterListEntry* character = findCharacter(objectId);
	if ((_session == nullptr) || (_step != EGameServerStep::CharacterSelect) || (character == nullptr))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeCharacterDelete(buffer, character->slot);
	send(buffer);

	setStep(EGameServerStep::DeleteSent);
	return true;
}

bool FGameServerFlow::restoreCharacter(uint32 objectId)
{
	const FCharacterListEntry* character = findCharacter(objectId);
	if ((_session == nullptr) || (_step != EGameServerStep::CharacterSelect) || (character == nullptr) || (character->deleteSeconds <= 0))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeCharacterRestore(buffer, character->slot);
	send(buffer);

	setStep(EGameServerStep::RestoreSent);
	return true;
}

bool FGameServerFlow::selectCharacter(uint32 objectId)
{
	const FCharacterListEntry* character = findCharacter(objectId);
	if ((_session == nullptr) || (_step != EGameServerStep::CharacterSelect) || (character == nullptr))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeCharacterSelect(buffer, character->slot);
	send(buffer);

	setStep(EGameServerStep::SelectSent);
	return true;
}

bool FGameServerFlow::enterWorld()
{
	if ((_session == nullptr) || (_step != EGameServerStep::Selected))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeEnterWorld(buffer);
	send(buffer);

	// Straight after EnterWorld, as the stock client does, so a server that kicks without it keeps the player.
	FWriteBuffer hardware;
	GameServer::writeRequestHardWareInfo(hardware);
	send(hardware);

	setStep(EGameServerStep::InWorld);
	return true;
}

bool FGameServerFlow::returnToCharacterSelect()
{
	if ((_session == nullptr) || (_step != EGameServerStep::InWorld))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeRequestRestart(buffer);
	send(buffer);

	setStep(EGameServerStep::Restarting);
	return true;
}

void FGameServerFlow::handlePacket(uint32 opcode, TArrayView<const uint8> body)
{
	if (_step == EGameServerStep::InWorld)
	{
		onWorldPacket.ExecuteIfBound(opcode, body);
		return;
	}

	FReadBuffer buffer(body);

	switch (opcode)
	{
		case GameServer::Opcode::restartResponse:
		{
			// One when the server lets the character go, which CharSelectionInfo follows; zero when it keeps it in the world.
			int32 result = 0;
			buffer.readInt32(result);
			if ((_step == EGameServerStep::Restarting) && (result == 0))
			{
				setStep(EGameServerStep::InWorld);
				onRestartRefused.ExecuteIfBound();
			}

			return;
		}
		case GameServer::Opcode::versionCheck:
		{
			TArray<uint8> key;
			bool encrypted = false;
			if (!GameServer::readKeyPacket(buffer, key, encrypted))
			{
				fail(TEXT("The game server refused this protocol revision."));
				return;
			}

			if (encrypted)
			{
				static_cast<FGameCipher*>(_session->cipher())->setKey(key);
			}

			FWriteBuffer reply;
			GameServer::writeAuthLogin(reply, _token.account, _token.playOk1, _token.playOk2, _token.loginOk1, _token.loginOk2);
			send(reply);

			setStep(EGameServerStep::Authenticating);
			return;
		}
		case GameServer::Opcode::loginFail:
		{
			int32 reason = 0;
			buffer.readInt32(reason);
			fail(FString::Printf(TEXT("The game server refused the login, reason %d."), reason));
			return;
		}
		case GameServer::Opcode::characterSelectionInfo:
		{
			if (!GameServer::readCharacterSelectionInfo(body, _characters))
			{
				fail(TEXT("The character list was malformed."));
				return;
			}

			// A fresh list ends whatever was waiting, so a screen listening for the step always hears it.
			_step = EGameServerStep::Idle;
			setStep(EGameServerStep::CharacterSelect);
			return;
		}
		case GameServer::Opcode::characterCreateSuccess:
		{
			FWriteBuffer request;
			GameServer::writeRequestGotoLobby(request);
			send(request);

			onCreated.ExecuteIfBound();
			return;
		}
		case GameServer::Opcode::characterCreateFail:
		{
			int32 reason = 0;
			buffer.readInt32(reason);
			setStep(EGameServerStep::CharacterSelect);
			onCreateRejected.ExecuteIfBound(static_cast<ECreateRejectReason>(reason));
			return;
		}
		case GameServer::Opcode::characterDeleteSuccess:
		{
			onDeleted.ExecuteIfBound();
			return;
		}
		case GameServer::Opcode::characterDeleteFail:
		{
			setStep(EGameServerStep::CharacterSelect);
			onDeleteRejected.ExecuteIfBound();
			return;
		}
		case GameServer::Opcode::characterSelected:
		{
			if (!GameServer::readCharacterSelected(buffer, _selected))
			{
				fail(TEXT("CharSelected was malformed."));
				return;
			}

			setStep(EGameServerStep::Selected);
			return;
		}
		default:
		{
			UE_LOG(LogAlverionGameServer, Verbose, TEXT("Ignored game packet 0x%X before the world."), opcode);
			return;
		}
	}
}

void FGameServerFlow::send(const FWriteBuffer& buffer)
{
	if (_session != nullptr)
	{
		_session->send(buffer.bytes());
	}
}

const FCharacterListEntry* FGameServerFlow::findCharacter(uint32 objectId) const
{
	return _characters.FindByPredicate([objectId](const FCharacterListEntry& candidate)
	{
		return candidate.objectId == objectId;
	});
}

void FGameServerFlow::setStep(EGameServerStep next)
{
	if (_step == next)
	{
		return;
	}

	_step = next;
	UE_LOG(LogAlverionGameServer, Display, TEXT("Game server step %d."), static_cast<int32>(next));
	onStepChanged.ExecuteIfBound(next);
}

void FGameServerFlow::fail(const FString& reason)
{
	if (_lastError.IsEmpty())
	{
		_lastError = reason;
	}

	UE_LOG(LogAlverionGameServer, Display, TEXT("%s"), *_lastError);

	setStep(EGameServerStep::Failed);

	if (_session != nullptr)
	{
		_session->disconnect();
	}

	onFailed.ExecuteIfBound(_lastError);
}

EGameServerStep FGameServerFlow::step() const
{
	return _step;
}

const TArray<FCharacterListEntry>& FGameServerFlow::characters() const
{
	return _characters;
}

const FCharacterSelected& FGameServerFlow::selected() const
{
	return _selected;
}

const FString& FGameServerFlow::lastError() const
{
	return _lastError;
}

FSession* FGameServerFlow::session() const
{
	return _session;
}
}
