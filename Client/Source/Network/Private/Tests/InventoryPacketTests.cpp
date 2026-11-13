#include "Core/WriteBuffer.h"
#include "GameServer/GameServerPackets.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// One item as AbstractItemPacket.writeItem lays it out, with the fields this client skips set to something other than zero.
	void inventoryTestWriteItem(Network::FWriteBuffer& body, int32 objectId, int32 itemId, int32 location, int64 count, int16 type2, bool equipped, int32 bodyPart, int16 enchant)
	{
		body.writeInt32(objectId);
		body.writeInt32(itemId);
		body.writeInt32(location);
		body.writeInt32(static_cast<int32>(count & 0xFFFFFFFF));
		body.writeInt32(static_cast<int32>(count >> 32));
		body.writeInt16(type2);
		body.writeInt16(0);
		body.writeInt16(equipped ? 1 : 0);
		body.writeInt32(bodyPart);
		body.writeInt16(enchant);
		body.writeInt16(7);
		body.writeInt32(11);
		body.writeInt32(-1);
		body.writeInt32(-9999);
		body.writeInt16(-2);
		body.writeInt16(0);
		for (int32 index = 0; index < 6 + 3; index++)
		{
			body.writeInt16(5);
		}
	}

	// An ItemList with gold in the bag and a worn chest piece.
	Network::FWriteBuffer inventoryTestItemList()
	{
		Network::FWriteBuffer body;
		body.writeInt16(1);
		body.writeInt16(2);
		inventoryTestWriteItem(body, 268480001, 57, 0, 5000000000LL, 4, false, 0, 0);
		inventoryTestWriteItem(body, 268480002, 1146, 6, 1, 1, true, 0x400, 3);
		body.writeInt16(0);
		return body;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemListTest, "Alverion.Network.Packets.ItemList", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FItemListTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer body = inventoryTestItemList();
	Network::FReadBuffer reader(body.bytes());
	TArray<Network::FInventoryItem> items;
	if (!TestTrue(TEXT("The list reads"), Network::GameServer::readItemList(reader, items)) || !TestEqual(TEXT("Both items"), items.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("Object id"), items[0].objectId, static_cast<uint32>(268480001));
	TestEqual(TEXT("Item id"), items[0].itemId, 57);
	TestEqual(TEXT("A count past four billion"), items[0].count, 5000000000LL);
	TestEqual(TEXT("Kind"), static_cast<int32>(items[0].type2), 4);
	TestFalse(TEXT("In the bag"), items[0].equipped);
	TestTrue(TEXT("Worn"), items[1].equipped);
	TestEqual(TEXT("Worn in the chest place"), items[1].location, 6);
	TestEqual(TEXT("Body part"), items[1].bodyPart, 0x400);
	TestEqual(TEXT("Enchant"), static_cast<int32>(items[1].enchant), 3);

	const TArrayView<const uint8> whole = body.bytes();
	Network::FReadBuffer shortReader(whole.Left(whole.Num() - 20));
	TestFalse(TEXT("A list cut short fails"), Network::GameServer::readItemList(shortReader, items));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryUpdateTest, "Alverion.Network.Packets.InventoryUpdate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInventoryUpdateTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer listBody = inventoryTestItemList();
	Network::FReadBuffer listReader(listBody.bytes());
	TArray<Network::FInventoryItem> items;
	Network::GameServer::readItemList(listReader, items);

	// The chest piece enchanted further, a new ring, and the gold spent.
	Network::FWriteBuffer body;
	body.writeInt16(3);
	body.writeInt16(2);
	inventoryTestWriteItem(body, 268480002, 1146, 6, 1, 1, true, 0x400, 4);
	body.writeInt16(1);
	inventoryTestWriteItem(body, 268480003, 875, 1, 1, 2, false, 0x30, 0);
	body.writeInt16(3);
	inventoryTestWriteItem(body, 268480001, 57, 0, 0, 4, false, 0, 0);

	Network::FReadBuffer reader(body.bytes());
	TArray<TPair<Network::GameServer::EItemChange, Network::FInventoryItem>> changes;
	if (!TestTrue(TEXT("The update reads"), Network::GameServer::readInventoryUpdate(reader, changes)) || !TestEqual(TEXT("Three changes"), changes.Num(), 3))
	{
		return false;
	}

	TestTrue(TEXT("The first modifies"), changes[0].Key == Network::GameServer::EItemChange::Modify);
	TestTrue(TEXT("The second adds"), changes[1].Key == Network::GameServer::EItemChange::Add);
	TestTrue(TEXT("The third removes"), changes[2].Key == Network::GameServer::EItemChange::Remove);

	Network::GameServer::applyInventoryUpdate(items, changes);
	TestEqual(TEXT("Two items left"), items.Num(), 2);
	const Network::FInventoryItem* chest = items.FindByPredicate([](const Network::FInventoryItem& item)
	{
		return item.objectId == 268480002;
	});

	TestTrue(TEXT("The chest piece took the new enchant"), (chest != nullptr) && (chest->enchant == 4));
	TestTrue(TEXT("The ring arrived"), items.ContainsByPredicate([](const Network::FInventoryItem& item)
	{
		return item.objectId == 268480003;
	}));

	TestFalse(TEXT("The gold is gone"), items.ContainsByPredicate([](const Network::FInventoryItem& item)
	{
		return item.objectId == 268480001;
	}));

	// An update that overtakes the list: a modified item not listed yet is added, and removing one that is not listed does nothing.
	TArray<Network::FInventoryItem> empty;
	Network::GameServer::applyInventoryUpdate(empty, changes);
	TestEqual(TEXT("The modified and added items land on an empty list"), empty.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShortcutPacketsTest, "Alverion.Network.Packets.Shortcuts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShortcutPacketsTest::RunTest(const FString& parameters)
{
	// ShortcutInit with an item, a skill and an action, each with its own tail.
	Network::FWriteBuffer body;
	body.writeInt32(3);
	body.writeInt32(1);
	body.writeInt32(0);
	body.writeInt32(268480001);
	body.writeInt32(1);
	body.writeInt32(0);
	body.writeInt32(0);
	body.writeInt32(0);
	body.writeInt16(0);
	body.writeInt16(0);
	body.writeInt32(2);
	body.writeInt32(3);
	body.writeInt32(1177);
	body.writeInt32(5);
	body.writeUInt8(0);
	body.writeInt32(1);
	body.writeInt32(3);
	body.writeInt32(13);
	body.writeInt32(2);
	body.writeInt32(1);

	Network::FReadBuffer reader(body.bytes());
	TArray<Network::FShortcut> shortcuts;
	if (!TestTrue(TEXT("The list reads"), Network::GameServer::readShortcutInit(reader, shortcuts)) || !TestEqual(TEXT("Three shortcuts"), shortcuts.Num(), 3))
	{
		return false;
	}

	TestTrue(TEXT("An item"), shortcuts[0].type == Network::EShortcutType::Item);
	TestEqual(TEXT("Naming the item's object id"), shortcuts[0].id, 268480001);
	TestTrue(TEXT("A skill"), shortcuts[1].type == Network::EShortcutType::Skill);
	TestEqual(TEXT("In slot 3"), shortcuts[1].slot, 3);
	TestEqual(TEXT("With its level"), shortcuts[1].level, 5);
	TestTrue(TEXT("An action"), shortcuts[2].type == Network::EShortcutType::Action);
	TestEqual(TEXT("On the second page"), shortcuts[2].slot, 13);
	TestEqual(TEXT("Nothing left over"), reader.remaining(), 0);

	// ShortcutRegister gives an item the same twenty bytes after its id.
	Network::FWriteBuffer registered;
	registered.writeInt32(1);
	registered.writeInt32(4);
	registered.writeInt32(268480002);
	for (int32 index = 0; index < 5; index++)
	{
		registered.writeInt32((index == 0) ? 1 : 0);
	}

	Network::FReadBuffer registeredReader(registered.bytes());
	Network::FShortcut shortcut;
	TestTrue(TEXT("A registered shortcut reads"), Network::GameServer::readShortcut(registeredReader, shortcut));
	TestEqual(TEXT("In slot 4"), shortcut.slot, 4);
	TestEqual(TEXT("And the whole of it"), registeredReader.remaining(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryRequestsTest, "Alverion.Network.Packets.InventoryRequests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInventoryRequestsTest::RunTest(const FString& parameters)
{
	Network::FWriteBuffer use;
	Network::GameServer::writeUseItem(use, 268480002);
	Network::FReadBuffer useReader(use.bytes());
	uint8 opcode = 0;
	int32 value = 0;
	int32 control = -1;
	useReader.readUInt8(opcode);
	useReader.readInt32(value);
	useReader.readInt32(control);
	TestEqual(TEXT("UseItem opcode"), static_cast<int32>(opcode), 0x19);
	TestEqual(TEXT("UseItem names the object"), value, 268480002);
	TestEqual(TEXT("Without Ctrl"), control, 0);

	Network::FWriteBuffer unequip;
	Network::GameServer::writeRequestUnEquipItem(unequip, 0x10);
	Network::FReadBuffer unequipReader(unequip.bytes());
	unequipReader.readUInt8(opcode);
	unequipReader.readInt32(value);
	TestEqual(TEXT("RequestUnEquipItem opcode"), static_cast<int32>(opcode), 0x16);
	TestEqual(TEXT("One finger"), value, 0x10);

	Network::FShortcut shortcut;
	shortcut.slot = 7;
	shortcut.type = Network::EShortcutType::Item;
	shortcut.id = 268480003;
	Network::FWriteBuffer reg;
	Network::GameServer::writeRequestShortcutReg(reg, shortcut);
	Network::FReadBuffer regReader(reg.bytes());
	int32 fields[5] = {};
	regReader.readUInt8(opcode);
	for (int32& field : fields)
	{
		regReader.readInt32(field);
	}

	TestEqual(TEXT("RequestShortcutReg opcode"), static_cast<int32>(opcode), 0x3D);
	TestTrue(TEXT("Type, slot, id, level and character type"), (fields[0] == 1) && (fields[1] == 7) && (fields[2] == 268480003) && (fields[3] == 0) && (fields[4] == 1));

	Network::FWriteBuffer del;
	Network::GameServer::writeRequestShortcutDel(del, 7);
	Network::FReadBuffer delReader(del.bytes());
	delReader.readUInt8(opcode);
	delReader.readInt32(value);
	TestEqual(TEXT("RequestShortcutDel opcode"), static_cast<int32>(opcode), 0x3F);
	TestEqual(TEXT("The slot"), value, 7);

	// More pairs than the server reads are cut to its limit.
	TArray<TPair<uint32, int32>> order;
	for (int32 index = 0; index < Network::GameServer::inventoryOrderLimit + 5; index++)
	{
		order.Add(TPair<uint32, int32>(268480000 + index, index));
	}

	Network::FWriteBuffer save;
	Network::GameServer::writeRequestSaveInventoryOrder(save, order);
	Network::FReadBuffer saveReader(save.bytes());
	int16 subOpcode = 0;
	int32 count = 0;
	saveReader.readUInt8(opcode);
	saveReader.readInt16(subOpcode);
	saveReader.readInt32(count);
	TestEqual(TEXT("Extended opcode"), static_cast<int32>(opcode), 0xD0);
	TestEqual(TEXT("RequestSaveInventoryOrder"), static_cast<int32>(subOpcode), 0x24);
	TestEqual(TEXT("Capped at the server's limit"), count, Network::GameServer::inventoryOrderLimit);
	TestEqual(TEXT("Two ints a pair"), saveReader.remaining(), count * 8);

	// A count past what an int holds, so both halves of the long are checked.
	Network::FWriteBuffer destroy;
	Network::GameServer::writeRequestDestroyItem(destroy, 268480002, 5000000000LL);
	Network::FReadBuffer destroyReader(destroy.bytes());
	int64 destroyed = 0;
	destroyReader.readUInt8(opcode);
	destroyReader.readInt32(value);
	destroyReader.readInt64(destroyed);
	TestEqual(TEXT("RequestDestroyItem opcode"), static_cast<int32>(opcode), 0x60);
	TestEqual(TEXT("RequestDestroyItem names the object"), value, 268480002);
	TestEqual(TEXT("And the count as a long"), destroyed, static_cast<int64>(5000000000LL));
	TestEqual(TEXT("Nothing after the count"), destroyReader.remaining(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUserInfoInventoryLimitTest, "Alverion.Network.Packets.UserInfoInventoryLimit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUserInfoInventoryLimitTest::RunTest(const FString& parameters)
{
	// UserInfo as far as the run speed, which is where the rest of what this client reads ends.
	Network::FWriteBuffer body;
	body.writeZeros(16);
	body.writeInt32(268480100);
	body.writeString(TEXT("Carrier"));
	body.writeZeros(12);
	body.writeInt32(20);
	body.writeZeros(16 + (10 * 4) + (4 * 4) + (3 * 26 * 4) + (2 * 4));
	body.writeZeros(4 + 4 + (5 * 4) + 8 + 4 + 8 + 4);

	Network::FReadBuffer shortReader(body.bytes());
	Network::FUserInfo info;
	TestTrue(TEXT("Stopping at the run speed still reads"), Network::GameServer::readUserInfo(shortReader, info));
	TestEqual(TEXT("And leaves the bag's size unknown"), info.inventoryLimit, 0);

	// The rest, as far as the bag's size, with a title and two cubics in the way.
	body.writeZeros((7 * 4) + (4 * 8) + (4 * 4));
	body.writeString(TEXT("Hauler"));
	body.writeZeros((4 * 4) + 4 + 3 + 8);
	body.writeInt16(2);
	body.writeZeros((2 * 2) + 1 + 4 + 1 + 4 + 2 + 2 + 4);
	body.writeInt16(100);

	Network::FReadBuffer reader(body.bytes());
	TestTrue(TEXT("The whole reads"), Network::GameServer::readUserInfo(reader, info));
	TestEqual(TEXT("The bag holds a hundred"), info.inventoryLimit, 100);
	TestEqual(TEXT("And the level is still right"), info.level, 20);
	return true;
}

#endif
