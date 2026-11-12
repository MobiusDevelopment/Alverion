#include "Core/WriteBuffer.h"
#include "GameServer/GameServerPackets.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// An NpcInfo body as AbstractNpcInfo.java lays it out up to the name, then the rest of the packet as zeros.
	Network::FWriteBuffer moveTypeTestNpcInfo(bool running)
	{
		Network::FWriteBuffer body;
		body.writeInt32(268480200);
		body.writeInt32(1000000 + 20001);
		body.writeInt32(1);
		body.writeInt32(1000);
		body.writeInt32(2000);
		body.writeInt32(-3500);
		body.writeInt32(0);
		body.writeInt32(0);
		body.writeInt32(333);
		body.writeInt32(300);
		body.writeInt32(120);
		body.writeInt32(50);
		for (int32 index = 0; index < 6; index++)
		{
			body.writeInt32(0);
		}

		for (int32 index = 0; index < 4; index++)
		{
			body.writeDouble(1.0);
		}

		body.writeInt32(0);
		body.writeInt32(0);
		body.writeInt32(0);
		body.writeUInt8(1);
		body.writeUInt8(running ? 1 : 0);
		body.writeUInt8(0);
		body.writeUInt8(0);
		body.writeUInt8(0);
		body.writeInt32(-1);
		body.writeString(TEXT("Goblin"));
		body.writeZeros(64);
		return body;
	}

	// A CharInfo body as CharInfo.java lays it out, with zeros in the fields this client does not read.
	Network::FWriteBuffer moveTypeTestCharInfo(bool running)
	{
		Network::FWriteBuffer body;
		body.writeInt32(1000);
		body.writeInt32(2000);
		body.writeInt32(-3500);
		body.writeInt32(0);
		body.writeInt32(268480300);
		body.writeString(TEXT("Walker"));
		body.writeInt32(0);
		body.writeInt32(0);
		body.writeInt32(0);

		// The paperdoll CharInfo shows others: the wooden sword in the right hand, the third slot, and nothing else.
		for (int32 slot = 0; slot < Network::GameServer::charInfoPaperdollSlots; slot++)
		{
			body.writeInt32((slot == 2) ? 30006 : 0);
		}

		body.writeZeros((Network::GameServer::charInfoPaperdollSlots + 7) * 4);
		body.writeInt32(120);
		body.writeInt32(50);
		body.writeInt32(80);
		body.writeZeros(5 * 4);
		body.writeDouble(1.0);
		body.writeZeros(24);
		body.writeInt32(0);
		body.writeInt32(0);
		body.writeInt32(0);
		body.writeString(TEXT(""));
		body.writeZeros(4 * 4);
		body.writeUInt8(1);
		body.writeUInt8(running ? 1 : 0);
		body.writeZeros(5);
		body.writeInt16(0);
		body.writeZeros(1 + 4 + 1 + 2 + (3 * 4) + 1 + 1 + 4 + 1 + 1 + 1 + (4 * 4));
		body.writeInt32(0);
		return body;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMoveTypeNpcInfoTest, "Alverion.Network.Packets.NpcInfoMoveType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMoveTypeNpcInfoTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer walking = moveTypeTestNpcInfo(false);
	Network::FCharacterSpawn walker;
	TestTrue(TEXT("A walking creature reads"), Network::GameServer::readNpcInfo(walking.bytes(), walker));
	TestFalse(TEXT("It walks"), walker.running);
	TestEqual(TEXT("Run speed in centimetres"), walker.runSpeed, 480.0f);
	TestEqual(TEXT("Walk speed in centimetres"), walker.walkSpeed, 200.0f);
	TestEqual(TEXT("The name is still read after the flags"), walker.name, FString(TEXT("Goblin")));

	Network::FWriteBuffer running = moveTypeTestNpcInfo(true);
	Network::FCharacterSpawn runner;
	TestTrue(TEXT("A running creature reads"), Network::GameServer::readNpcInfo(running.bytes(), runner));
	TestTrue(TEXT("It runs"), runner.running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMoveTypeCharInfoTest, "Alverion.Network.Packets.CharInfoMoveType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMoveTypeCharInfoTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer walking = moveTypeTestCharInfo(false);
	Network::FCharacterSpawn walker;
	TestTrue(TEXT("A walking player reads"), Network::GameServer::readCharInfo(walking.bytes(), walker));
	TestFalse(TEXT("It walks"), walker.running);
	TestEqual(TEXT("Run speed in centimetres"), walker.runSpeed, 480.0f);
	TestEqual(TEXT("Walk speed in centimetres"), walker.walkSpeed, 200.0f);
	TestEqual(TEXT("The name is read"), walker.name, FString(TEXT("Walker")));
	TestEqual(TEXT("Every slot CharInfo shows is read"), walker.worn.Num(), Network::GameServer::charInfoPaperdollSlots);
	TestEqual(TEXT("With the sword in the right hand"), walker.worn.IsValidIndex(2) ? walker.worn[2] : 0, 30006);

	Network::FWriteBuffer running = moveTypeTestCharInfo(true);
	Network::FCharacterSpawn runner;
	TestTrue(TEXT("A running player reads"), Network::GameServer::readCharInfo(running.bytes(), runner));
	TestTrue(TEXT("It runs"), runner.running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChangeMoveTypeTest, "Alverion.Network.Packets.ChangeMoveType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChangeMoveTypeTest::RunTest(const FString& parameters)
{
	// A body as ChangeMoveType.java writes it: the object, 0 for walking or 1 for running, and a zero.
	Network::FWriteBuffer body;
	body.writeInt32(268480200);
	body.writeInt32(0);
	body.writeInt32(0);

	Network::FReadBuffer walkReader(body.bytes());
	uint32 objectId = 0;
	bool running = true;
	TestTrue(TEXT("The change reads"), Network::GameServer::readChangeMoveType(walkReader, objectId, running));
	TestEqual(TEXT("Object id"), objectId, static_cast<uint32>(268480200));
	TestFalse(TEXT("Type 0 is walking"), running);

	Network::FWriteBuffer runBody;
	runBody.writeInt32(268480200);
	runBody.writeInt32(1);
	runBody.writeInt32(0);
	Network::FReadBuffer runReader(runBody.bytes());
	TestTrue(TEXT("The second change reads"), Network::GameServer::readChangeMoveType(runReader, objectId, running));
	TestTrue(TEXT("Type 1 is running"), running);

	const TArrayView<const uint8> nothing;
	Network::FReadBuffer emptyReader(nothing);
	TestFalse(TEXT("An empty body fails"), Network::GameServer::readChangeMoveType(emptyReader, objectId, running));
	return true;
}

#endif
