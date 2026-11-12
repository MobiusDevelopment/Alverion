#include "Core/WriteBuffer.h"
#include "GameServer/GameServerPackets.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActionTest, "Alverion.Network.Packets.Action", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActionTest::RunTest(const FString& parameters)
{
	// The origin is where the player stands, in the server's units, and the action byte is zero for a plain click.
	Network::FWriteBuffer buffer;
	Network::GameServer::writeAction(buffer, 268480123, FVector(400.0, -800.0, Network::toCentimetres(-3500 - Network::seaLevelUnits)));

	Network::FReadBuffer reader(buffer.bytes());
	uint8 opcode = 0;
	int32 objectId = 0;
	int32 x = 0;
	int32 y = 0;
	int32 z = 0;
	uint8 action = 1;
	reader.readUInt8(opcode);
	reader.readInt32(objectId);
	reader.readInt32(x);
	reader.readInt32(y);
	reader.readInt32(z);
	reader.readUInt8(action);

	TestEqual(TEXT("Action opcode"), static_cast<int32>(opcode), 0x1F);
	TestEqual(TEXT("Object id"), objectId, 268480123);
	TestEqual(TEXT("Origin X in server units"), x, 100);
	TestEqual(TEXT("Origin Y in server units"), y, -200);
	TestEqual(TEXT("Origin Z in server units"), z, -3500);
	TestEqual(TEXT("A plain click"), static_cast<int32>(action), 0);
	TestFalse(TEXT("Every field reads back"), reader.hasFailed());
	TestEqual(TEXT("Nothing left over"), reader.remaining(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestBypassToServerTest, "Alverion.Network.Packets.RequestBypassToServer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRequestBypassToServerTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer buffer;
	TestTrue(TEXT("The command is written"), Network::GameServer::writeRequestBypassToServer(buffer, TEXT("npc_268480123_Quest")));

	Network::FReadBuffer reader(buffer.bytes());
	uint8 opcode = 0;
	FString command;
	reader.readUInt8(opcode);
	reader.readString(command);

	TestEqual(TEXT("RequestBypassToServer opcode"), static_cast<int32>(opcode), 0x23);
	TestEqual(TEXT("The command comes back as sent"), command, FString(TEXT("npc_268480123_Quest")));
	TestEqual(TEXT("Nothing left over"), reader.remaining(), 0);

	// A command with a zero in it cannot be written, so it never goes out half sent.
	Network::FWriteBuffer refused;
	const FString broken = FString::ConstructFromPtrSize(TEXT("a\0b"), 3);
	TestFalse(TEXT("A command holding a zero is refused"), Network::GameServer::writeRequestBypassToServer(refused, broken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestLinkHtmlTest, "Alverion.Network.Packets.RequestLinkHtml", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRequestLinkHtmlTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer buffer;
	TestTrue(TEXT("The file name is written"), Network::GameServer::writeRequestLinkHtml(buffer, TEXT("default/30006-1.htm")));

	Network::FReadBuffer reader(buffer.bytes());
	uint8 opcode = 0;
	FString file;
	reader.readUInt8(opcode);
	reader.readString(file);

	TestEqual(TEXT("RequestLinkHtml opcode"), static_cast<int32>(opcode), 0x22);
	TestEqual(TEXT("The file name comes back as sent"), file, FString(TEXT("default/30006-1.htm")));
	TestEqual(TEXT("Nothing left over"), reader.remaining(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcHtmlMessageTest, "Alverion.Network.Packets.NpcHtmlMessage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNpcHtmlMessageTest::RunTest(const FString& parameters)
{
	// A body as NpcHtmlMessage.java writes it: the townsperson's object id, the page, and the item id.
	const FString page = TEXT("<html><body>Hello, <a action=\"bypass -h npc_1_Quest\">traveller</a>.</body></html>");
	Network::FWriteBuffer body;
	body.writeInt32(268480123);
	body.writeString(page);
	body.writeInt32(0);

	Network::FReadBuffer reader(body.bytes());
	Network::FNpcHtml message;
	TestTrue(TEXT("The page reads"), Network::GameServer::readNpcHtmlMessage(reader, message));
	TestEqual(TEXT("Nothing left over"), reader.remaining(), 0);
	TestEqual(TEXT("Townsperson"), message.npcObjectId, static_cast<uint32>(268480123));
	TestEqual(TEXT("The page comes back as sent"), message.html, page);
	TestEqual(TEXT("Item id"), message.itemId, 0);

	// A body that stops inside the page fails the read rather than showing half of it.
	TArrayView<const uint8> whole = body.bytes();
	Network::FReadBuffer shortReader(whole.Left(whole.Num() - 10));
	Network::FNpcHtml broken;
	TestFalse(TEXT("A short body fails"), Network::GameServer::readNpcHtmlMessage(shortReader, broken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetPacketsTest, "Alverion.Network.Packets.Target", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTargetPacketsTest::RunTest(const FString& parameters)
{
	// MyTargetSelected carries the object, a level colour and a zero; TargetUnselected carries the object and its place. Only the object is read from both.
	Network::FWriteBuffer selected;
	selected.writeInt32(268480123);
	selected.writeInt16(0);
	selected.writeInt32(0);

	Network::FReadBuffer selectedReader(selected.bytes());
	uint32 objectId = 0;
	TestTrue(TEXT("MyTargetSelected reads"), Network::GameServer::readMyTargetSelected(selectedReader, objectId));
	TestEqual(TEXT("Selected object"), objectId, static_cast<uint32>(268480123));

	Network::FWriteBuffer unselected;
	unselected.writeInt32(268480124);
	unselected.writeInt32(1000);
	unselected.writeInt32(2000);
	unselected.writeInt32(-3500);
	unselected.writeInt32(0);

	Network::FReadBuffer unselectedReader(unselected.bytes());
	objectId = 0;
	TestTrue(TEXT("TargetUnselected reads"), Network::GameServer::readTargetUnselected(unselectedReader, objectId));
	TestEqual(TEXT("Unselected object"), objectId, static_cast<uint32>(268480124));

	const TArrayView<const uint8> nothing;
	Network::FReadBuffer emptyReader(nothing);
	TestFalse(TEXT("An empty body fails"), Network::GameServer::readMyTargetSelected(emptyReader, objectId));
	return true;
}

#endif
