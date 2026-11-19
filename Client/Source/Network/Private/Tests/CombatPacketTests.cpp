#if WITH_DEV_AUTOMATION_TESTS

#include "Core/ReadBuffer.h"
#include "Core/WriteBuffer.h"
#include "GameServer/GameServerPackets.h"
#include "Misc/AutomationTest.h"

namespace
{
	// One hit as Attack.java writes it.
	void combatTestHit(Network::FWriteBuffer& body, int32 target, int32 damage, uint8 flags)
	{
		body.writeInt32(target);
		body.writeInt32(damage);
		body.writeUInt8(flags);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPacketsTest, "Alverion.Network.Packets.Combat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatPacketsTest::RunTest(const FString& parameters)
{
	// A polearm's sweep: a critical on the first target, then a miss on a second, with the places between them as Attack.java lays it out.
	Network::FWriteBuffer sweep;
	sweep.writeInt32(268480100);
	combatTestHit(sweep, 268480002, 57, Network::GameServer::hitCritical);
	sweep.writeInt32(1000);
	sweep.writeInt32(2000);
	sweep.writeInt32(-3500);
	sweep.writeInt16(1);
	combatTestHit(sweep, 268480003, 0, Network::GameServer::hitMissed);
	sweep.writeInt32(1040);
	sweep.writeInt32(2000);
	sweep.writeInt32(-3500);

	Network::FAttack attack;
	TestTrue(TEXT("An Attack reads"), Network::GameServer::readAttack(sweep.bytes(), attack));
	TestEqual(TEXT("The attacker"), attack.attackerId, 268480100u);
	if (TestEqual(TEXT("Both hits"), attack.hits.Num(), 2))
	{
		TestEqual(TEXT("The first target"), attack.hits[0].targetId, 268480002u);
		TestEqual(TEXT("Its damage"), attack.hits[0].damage, 57);
		TestEqual(TEXT("A critical"), attack.hits[0].flags, Network::GameServer::hitCritical);
		TestEqual(TEXT("The second target, after the attacker's place and the count"), attack.hits[1].targetId, 268480003u);
		TestEqual(TEXT("A miss"), attack.hits[1].flags, Network::GameServer::hitMissed);
	}

	// Cut short before the count, which a reader must not take for a whole attack.
	const TArray<uint8> whole(sweep.bytes());
	Network::FAttack cut;
	TestFalse(TEXT("A truncated Attack is refused"), Network::GameServer::readAttack(TArrayView<const uint8>(whole.GetData(), 20), cut));

	// Die: the one who fell, then the restart options this client does not offer.
	Network::FWriteBuffer die;
	die.writeInt32(268480002);
	die.writeInt32(1);
	die.writeZeros(6 * 4);
	uint32 fallen = 0;
	TestTrue(TEXT("A Die reads"), Network::GameServer::readCombatObjectId(die.bytes(), fallen));
	TestEqual(TEXT("And names who fell"), fallen, 268480002u);

	Network::FWriteBuffer restart;
	Network::GameServer::writeRequestRestartPoint(restart, Network::GameServer::restartPointVillage);
	Network::FReadBuffer restartReader(restart.bytes());
	uint8 opcode = 0;
	int32 point = -1;
	restartReader.readUInt8(opcode);
	restartReader.readInt32(point);
	TestEqual(TEXT("RequestRestartPoint opcode"), static_cast<int32>(opcode), 0x7D);
	TestEqual(TEXT("To the village"), point, 0);
	TestEqual(TEXT("Nothing after the point"), restartReader.remaining(), 0);

	// MoveToPawn: a monster 100 units off following someone it means to come within 40 units of, so it heads 60 units along the line between them.
	Network::FWriteBuffer follow;
	follow.writeInt32(268480100);
	follow.writeInt32(268480002);
	follow.writeInt32(40);
	follow.writeInt32(0);
	follow.writeInt32(0);
	follow.writeInt32(-3500);
	follow.writeInt32(100);
	follow.writeInt32(0);
	follow.writeInt32(-3500);
	Network::FReadBuffer followReader(follow.bytes());
	Network::FObjectPlace chase;
	TestTrue(TEXT("A MoveToPawn reads"), Network::GameServer::readMoveToPawn(followReader, chase));
	TestEqual(TEXT("For the follower"), chase.objectId, 268480100u);
	TestTrue(TEXT("Headed short of the one it follows"), chase.location.Equals(Network::toLocation(60, 0, -3500), 0.01));

	// The corpse loot list, after its extended id: a stack of adena past what an int holds, and a shirt.
	Network::FWriteBuffer loot;
	loot.writeInt32(268480100);
	loot.writeInt32(2);
	loot.writeInt32(57);
	loot.writeInt32(static_cast<int32>(5000000000LL & 0xFFFFFFFF));
	loot.writeInt32(static_cast<int32>(5000000000LL >> 32));
	loot.writeInt32(30001);
	loot.writeInt32(1);
	loot.writeInt32(0);
	Network::FCorpseLoot corpse;
	TestTrue(TEXT("A loot list reads"), Network::GameServer::readCorpseLoot(loot.bytes(), corpse));
	TestEqual(TEXT("For the corpse"), corpse.corpseId, 268480100u);
	if (TestEqual(TEXT("Both stacks"), corpse.stacks.Num(), 2))
	{
		TestEqual(TEXT("Adena first"), corpse.stacks[0].itemId, 57);
		TestEqual(TEXT("Its count as a long"), corpse.stacks[0].count, static_cast<int64>(5000000000LL));
		TestEqual(TEXT("Then the shirt"), corpse.stacks[1].itemId, 30001);
	}

	// Taking the shirt: the stack's place and its item, after the extended id and the action.
	Network::FWriteBuffer take;
	Network::GameServer::writeRequestCorpseLoot(take, Network::GameServer::ELootAction::Take, 268480100, 1, 30001);
	Network::FReadBuffer takeReader(take.bytes());
	uint8 takeOpcode = 0;
	int16 takeSubOpcode = 0;
	uint8 action = 0;
	int32 takeCorpse = 0;
	int32 takeIndex = 0;
	int32 takeItem = 0;
	takeReader.readUInt8(takeOpcode);
	takeReader.readInt16(takeSubOpcode);
	takeReader.readUInt8(action);
	takeReader.readInt32(takeCorpse);
	takeReader.readInt32(takeIndex);
	takeReader.readInt32(takeItem);
	TestEqual(TEXT("Extended opcode"), static_cast<int32>(takeOpcode), 0xD0);
	TestEqual(TEXT("RequestCorpseLoot"), static_cast<int32>(takeSubOpcode), 0x98);
	TestEqual(TEXT("A take"), static_cast<int32>(action), 1);
	TestEqual(TEXT("Of the corpse"), takeCorpse, 268480100);
	TestEqual(TEXT("The stack's place"), takeIndex, 1);
	TestEqual(TEXT("And its item"), takeItem, 30001);
	TestEqual(TEXT("Nothing after the item"), takeReader.remaining(), 0);
	return true;
}

#endif
