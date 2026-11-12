#include "GameServer/WorldFlow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAlverionWorld, Display, All);

namespace Network
{
namespace
{
	// How far ahead of the character the keyboard destination goes. Far enough that the server never sees it arrive between reports.
	constexpr double moveLookaheadCm = 600.0;

	// A held key is reported again this often, and at once when the direction swings by more than about twenty degrees.
	constexpr float moveResendSeconds = 0.5f;
	constexpr double moveResendCosine = 0.94;

	// How often the position goes out while moving. The server takes it as true in keyboard mode.
	constexpr float validateSeconds = 0.25f;

	// Slower than this is standing still, so a character settling onto the ground does not stream moves.
	constexpr double movingSpeedCm = 20.0;

	// A correction smaller than this is the server repeating where this client already said it was.
	constexpr double correctionToleranceCm = 150.0;

	// The server drops a click that follows another within FloodProtectorPlayerActionInterval, one tick of a tenth of a second on High Five, so the second click of a talk keeps well clear of the first.
	constexpr float actionGapSeconds = 0.35f;

	// A selection the server has not confirmed by now is not coming.
	constexpr float talkGiveUpSeconds = 2.0f;
}

FWorldFlow::~FWorldFlow()
{
	end();
}

bool FWorldFlow::begin(FGameServerFlow& gameServer)
{
	_gameServer = &gameServer;
	_localObjectId = gameServer.selected().objectId;
	_userInfo = FUserInfo();
	_items.Reset();
	_shortcuts.Reset();
	_revision++;
	_hasLocalState = false;
	_moving = false;
	_sinceMoveSent = 0.0f;
	_sinceValidateSent = 0.0f;
	_targetObjectId = 0;
	_talkObjectId = 0;
	_talkSelected = false;
	_talkSeconds = 0.0f;
	_sinceActionSent = actionGapSeconds;

	_gameServer->onWorldPacket = FPacketHandler::CreateRaw(this, &FWorldFlow::handlePacket);
	return _gameServer->enterWorld();
}

void FWorldFlow::end()
{
	if (_gameServer != nullptr)
	{
		_gameServer->onWorldPacket = FPacketHandler();
		_gameServer = nullptr;
	}
}

bool FWorldFlow::isActive() const
{
	return _gameServer != nullptr;
}

const FUserInfo& FWorldFlow::userInfo() const
{
	return _userInfo;
}

const TArray<FInventoryItem>& FWorldFlow::items() const
{
	return _items;
}

const TMap<int32, FShortcut>& FWorldFlow::shortcuts() const
{
	return _shortcuts;
}

uint32 FWorldFlow::revision() const
{
	return _revision;
}

bool FWorldFlow::canSend() const
{
	return (_gameServer != nullptr) && (_gameServer->session() != nullptr) && (_gameServer->step() == EGameServerStep::InWorld);
}

bool FWorldFlow::useItem(uint32 objectId)
{
	if (!canSend() || (objectId == 0))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeUseItem(buffer, objectId);
	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::unequip(int32 bodyPartMask)
{
	if (!canSend() || (bodyPartMask == 0))
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeRequestUnEquipItem(buffer, bodyPartMask);
	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::setShortcut(const FShortcut& shortcut)
{
	if (!canSend() || (shortcut.type == EShortcutType::None))
	{
		return false;
	}

	_shortcuts.Add(shortcut.slot, shortcut);
	_revision++;

	FWriteBuffer buffer;
	GameServer::writeRequestShortcutReg(buffer, shortcut);
	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::clearShortcut(int32 slot)
{
	if (!canSend())
	{
		return false;
	}

	_shortcuts.Remove(slot);
	_revision++;

	FWriteBuffer buffer;
	GameServer::writeRequestShortcutDel(buffer, slot);
	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::saveInventoryOrder(TConstArrayView<TPair<uint32, int32>> order)
{
	if (!canSend() || order.IsEmpty())
	{
		return false;
	}

	for (const TPair<uint32, int32>& place : order)
	{
		FInventoryItem* item = _items.FindByPredicate([&place](const FInventoryItem& candidate)
		{
			return candidate.objectId == place.Key;
		});

		if ((item != nullptr) && !item->equipped)
		{
			item->location = place.Value;
		}
	}

	_revision++;

	FWriteBuffer buffer;
	GameServer::writeRequestSaveInventoryOrder(buffer, order);
	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::say(const FString& text, EChatChannel channel)
{
	if ((_gameServer == nullptr) || (_gameServer->session() == nullptr) || text.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}

	FWriteBuffer buffer;
	GameServer::writeSay2(buffer, text, channel);
	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::talkTo(uint32 objectId)
{
	if ((_gameServer == nullptr) || (_gameServer->session() == nullptr) || (_gameServer->step() != EGameServerStep::InWorld) || (objectId == 0))
	{
		return false;
	}

	_talkObjectId = objectId;
	_talkSeconds = 0.0f;

	// An object that is selected already needs only the second click, which tick sends once the gap since the last click has passed.
	_talkSelected = (_targetObjectId == objectId);
	if (!_talkSelected)
	{
		sendAction(objectId);
	}

	return true;
}

bool FWorldFlow::bypass(const FString& command)
{
	if ((_gameServer == nullptr) || (_gameServer->session() == nullptr) || command.IsEmpty())
	{
		return false;
	}

	FWriteBuffer buffer;
	if (!GameServer::writeRequestBypassToServer(buffer, command))
	{
		return false;
	}

	_gameServer->session()->send(buffer.bytes());
	return true;
}

bool FWorldFlow::link(const FString& file)
{
	if ((_gameServer == nullptr) || (_gameServer->session() == nullptr) || file.IsEmpty())
	{
		return false;
	}

	FWriteBuffer buffer;
	if (!GameServer::writeRequestLinkHtml(buffer, file))
	{
		return false;
	}

	_gameServer->session()->send(buffer.bytes());
	return true;
}

void FWorldFlow::setLocalState(const FVector& location, float yaw, bool wantsToMove)
{
	_location = location;
	_yaw = yaw;
	_wantsToMove = wantsToMove;

	if (!_hasLocalState)
	{
		_previousLocation = location;
		_hasLocalState = true;
	}
}

void FWorldFlow::tick(float deltaSeconds)
{
	// Nothing goes out while a restart is pending, since a server that agrees has already taken the character out of the world.
	if ((_gameServer == nullptr) || (deltaSeconds <= 0.0f) || (_gameServer->step() != EGameServerStep::InWorld))
	{
		return;
	}

	_sinceActionSent += deltaSeconds;
	if (_talkObjectId != 0)
	{
		_talkSeconds += deltaSeconds;
		if (_talkSelected && (_sinceActionSent >= actionGapSeconds))
		{
			sendAction(_talkObjectId);
			_talkObjectId = 0;
		}
		else if (_talkSeconds >= talkGiveUpSeconds)
		{
			_talkObjectId = 0;
		}
	}

	if (!_hasLocalState)
	{
		return;
	}

	_sinceMoveSent += deltaSeconds;
	_sinceValidateSent += deltaSeconds;

	// The direction comes from how the character actually moved, so it agrees with whatever the camera made of the keys.
	const FVector step = _location - _previousLocation;
	_previousLocation = _location;

	const double speed = step.Size2D() / static_cast<double>(deltaSeconds);
	if (_wantsToMove && (speed > movingSpeedCm))
	{
		const FVector direction = step.GetSafeNormal2D();
		const bool turned = (FVector::DotProduct(direction, _moveDirection) < moveResendCosine);

		if (!_moving || turned || (_sinceMoveSent >= moveResendSeconds))
		{
			_moveDirection = direction;
			sendMove(_location + (direction * moveLookaheadCm));
		}

		if (_sinceValidateSent >= validateSeconds)
		{
			sendValidate();
		}

		_moving = true;
		return;
	}

	if (_moving)
	{
		// A destination equal to the origin is how a client says stop.
		sendMove(_location);
		sendValidate();
		_moving = false;
	}
}

void FWorldFlow::sendMove(const FVector& target)
{
	FWriteBuffer buffer;
	GameServer::writeMoveToLocation(buffer, target, _location);
	_gameServer->session()->send(buffer.bytes());
	_sinceMoveSent = 0.0f;
}

void FWorldFlow::sendValidate()
{
	FWriteBuffer buffer;
	GameServer::writeValidatePosition(buffer, _location, _yaw);
	_gameServer->session()->send(buffer.bytes());
	_sinceValidateSent = 0.0f;
}

void FWorldFlow::sendAction(uint32 objectId)
{
	FWriteBuffer buffer;
	GameServer::writeAction(buffer, objectId, _location);
	_gameServer->session()->send(buffer.bytes());
	_sinceActionSent = 0.0f;
}

void FWorldFlow::handlePacket(uint32 opcode, TArrayView<const uint8> body)
{
	FReadBuffer buffer(body);

	switch (opcode)
	{
		case GameServer::Opcode::userInfo:
		{
			FUserInfo info;
			if (GameServer::readUserInfo(buffer, info))
			{
				_localObjectId = info.objectId;
				_userInfo = info;
			}

			return;
		}
		case GameServer::Opcode::itemList:
		{
			TArray<FInventoryItem> items;
			if (GameServer::readItemList(buffer, items))
			{
				_items = MoveTemp(items);
				_revision++;
			}

			return;
		}
		case GameServer::Opcode::inventoryUpdate:
		{
			TArray<TPair<GameServer::EItemChange, FInventoryItem>> changes;
			if (GameServer::readInventoryUpdate(buffer, changes))
			{
				GameServer::applyInventoryUpdate(_items, changes);
				_revision++;
			}

			return;
		}
		case GameServer::Opcode::shortcutInit:
		{
			TArray<FShortcut> shortcuts;
			if (GameServer::readShortcutInit(buffer, shortcuts))
			{
				_shortcuts.Reset();
				for (const FShortcut& shortcut : shortcuts)
				{
					_shortcuts.Add(shortcut.slot, shortcut);
				}

				_revision++;
			}

			return;
		}
		case GameServer::Opcode::shortcutRegister:
		{
			FShortcut shortcut;
			if (GameServer::readShortcut(buffer, shortcut))
			{
				_shortcuts.Add(shortcut.slot, shortcut);
				_revision++;
			}

			return;
		}
		case GameServer::Opcode::statusUpdate:
		{
			// The server sends these for every creature in sight; only this client's own character has bars to keep.
			FStatusUpdate update;
			if (GameServer::readStatusUpdate(buffer, update) && (_localObjectId != 0) && (update.objectId == _localObjectId))
			{
				GameServer::applyStatusUpdate(update, _userInfo);
			}

			return;
		}
		case GameServer::Opcode::changeMoveType:
		{
			uint32 objectId = 0;
			bool running = true;
			if (GameServer::readChangeMoveType(buffer, objectId, running) && (objectId != _localObjectId))
			{
				onMoveTypeChanged.ExecuteIfBound(objectId, running);
			}

			return;
		}
		case GameServer::Opcode::creatureSay:
		{
			FChatMessage message;
			if (GameServer::readCreatureSay(buffer, message))
			{
				onChat.ExecuteIfBound(message);
			}

			return;
		}
		case GameServer::Opcode::npcHtmlMessage:
		{
			FNpcHtml message;
			if (GameServer::readNpcHtmlMessage(buffer, message))
			{
				onNpcHtml.ExecuteIfBound(message);
			}

			return;
		}
		case GameServer::Opcode::myTargetSelected:
		{
			uint32 objectId = 0;
			if (GameServer::readMyTargetSelected(buffer, objectId))
			{
				_targetObjectId = objectId;
				_talkSelected = _talkSelected || (objectId == _talkObjectId);
			}

			return;
		}
		case GameServer::Opcode::targetUnselected:
		{
			uint32 objectId = 0;
			if (GameServer::readTargetUnselected(buffer, objectId) && (objectId == _targetObjectId))
			{
				_targetObjectId = 0;
			}

			return;
		}
		case GameServer::Opcode::charInfo:
		{
			FCharacterSpawn spawn;
			if (GameServer::readCharInfo(body, spawn) && (spawn.objectId != _localObjectId))
			{
				onSpawned.ExecuteIfBound(spawn);
			}

			return;
		}
		case GameServer::Opcode::npcInfo:
		{
			FCharacterSpawn spawn;
			if (GameServer::readNpcInfo(body, spawn))
			{
				onSpawned.ExecuteIfBound(spawn);
			}
			else
			{
				UE_LOG(LogAlverionWorld, Warning, TEXT("NpcInfo of %d bytes did not parse."), body.Num());
			}

			return;
		}
		case GameServer::Opcode::deleteObject:
		{
			int32 objectId = 0;
			if (buffer.readInt32(objectId))
			{
				onDespawned.ExecuteIfBound(static_cast<uint32>(objectId));
			}

			return;
		}
		case GameServer::Opcode::moveToLocation:
		{
			// This client's own moves come back to it as well, and it is already where it says it is going.
			FObjectPlace destination;
			if (GameServer::readObjectPlace(buffer, destination) && (destination.objectId != _localObjectId))
			{
				onMoved.ExecuteIfBound(destination, EMoveKind::Destination);
			}

			return;
		}
		case GameServer::Opcode::stopMove:
		case GameServer::Opcode::validateLocation:
		case GameServer::Opcode::teleportToLocation:
		{
			const bool teleport = (opcode == GameServer::Opcode::teleportToLocation);

			FObjectPlace place;
			if (!GameServer::readObjectPlaceWithHeading(buffer, place, teleport))
			{
				return;
			}

			if (place.objectId != _localObjectId)
			{
				onMoved.ExecuteIfBound(place, EMoveKind::Stopped);
				return;
			}

			if (teleport)
			{
				onCorrected.ExecuteIfBound(place, true);

				// The jump is not a run, so speed is measured again from wherever the character lands.
				_hasLocalState = false;
				_moving = false;

				FWriteBuffer reply;
				GameServer::writeAppearing(reply);
				_gameServer->session()->send(reply.bytes());
				return;
			}

			// Keyboard mode makes this client the authority, so the server echoing its own position is not a correction.
			if (FVector::Dist2D(place.location, _location) > correctionToleranceCm)
			{
				onCorrected.ExecuteIfBound(place, false);
			}

			return;
		}
		default:
		{
			UE_LOG(LogAlverionWorld, Verbose, TEXT("Ignored world packet 0x%X, %d bytes."), opcode, body.Num());
			return;
		}
	}
}
}
