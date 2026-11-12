#include "GameServer/GameServerFlow.h"
#include "LoginServer/LoginServerFlow.h"
#include "Misc/AutomationTest.h"
#include "GameServer/WorldFlow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A dev server with AutoCreateAccounts on makes this account the first time it is used.
	const TCHAR* const probeAccount = TEXT("alverionprobe");
	const TCHAR* const probePassword = TEXT("alverionprobe");
	// Named for the appearance layout it carries, so a probe made under an older layout is never mistaken for this one.
	const TCHAR* const probeCharacter = TEXT("AlvProbeA1");

	Network::FAppearance probeAppearance()
	{
		Network::FAppearance appearance;
		appearance.male = true;
		appearance.head = 2;
		appearance.skinTone = 3;
		appearance.eyeColor = 4;
		appearance.hairStyle = 9;
		appearance.hairColor = 2;
		appearance.eyebrows = 1;
		appearance.beard = 5;
		appearance.mustache = 6;
		return appearance;
	}

	/** Pumps both links and ticks every flow until the condition holds or the time runs out. */
	bool pumpUntil(Network::FSession& loginSession, Network::FSession& gameSession, Network::FLoginServerFlow& loginFlow, Network::FGameServerFlow& gameFlow, Network::FWorldFlow& worldFlow, TFunctionRef<bool()> condition, double timeoutSeconds)
	{
		const double deadline = FPlatformTime::Seconds() + timeoutSeconds;
		while (FPlatformTime::Seconds() < deadline)
		{
			loginSession.pump(256);
			gameSession.pump(256);
			loginFlow.tick();
			gameFlow.tick();
			worldFlow.tick(0.01f);

			if (condition())
			{
				return true;
			}

			FPlatformProcess::Sleep(0.01f);
		}

		return false;
	}
}

/**
 * Against a running login and game server, with the ServerPatch appearance extension applied.
 * Named Live so an ordinary test pass, which has no server, never runs it by accident.
 * The host defaults to this machine; -serverhost= names another.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveServerRoundTripTest, "Alverion.Live.ServerRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLiveServerRoundTripTest::RunTest(const FString& parameters)
{
	FString host = TEXT("127.0.0.1");
	FParse::Value(FCommandLine::Get(), TEXT("serverhost="), host);

	Network::FSession loginSession;
	Network::FSession gameSession;
	Network::FLoginServerFlow loginFlow;
	Network::FGameServerFlow gameFlow;
	Network::FWorldFlow worldFlow;

	auto pump = [&](TFunctionRef<bool()> condition, double timeoutSeconds)
	{
		return pumpUntil(loginSession, gameSession, loginFlow, gameFlow, worldFlow, condition, timeoutSeconds);
	};

	loginSession.connect(MakeUnique<Network::FTcpTransport>(), host, 2106, MakeUnique<Network::FLoginCipher>());
	loginFlow.begin(loginSession, probeAccount, probePassword);

	const bool listed = pump([&]() { return (loginFlow.step() == Network::ELoginServerStep::ServerSelect) || (loginFlow.step() == Network::ELoginServerStep::Failed); }, 10.0);
	if (!TestTrue(TEXT("The login server answers with a server list"), listed && (loginFlow.step() == Network::ELoginServerStep::ServerSelect)))
	{
		AddError(loginFlow.lastError());
		return false;
	}

	const Network::FServerListEntry* online = loginFlow.servers().FindByPredicate([](const Network::FServerListEntry& entry) { return entry.status == Network::EServerStatus::Online; });
	if (!TestNotNull(TEXT("A game server is online"), online))
	{
		return false;
	}

	loginFlow.selectServer(online->serverId);
	const bool issued = pump([&]() { return (loginFlow.step() == Network::ELoginServerStep::TokenIssued) || (loginFlow.step() == Network::ELoginServerStep::Failed); }, 10.0);
	if (!TestTrue(TEXT("The login server issues a play token"), issued && (loginFlow.step() == Network::ELoginServerStep::TokenIssued)))
	{
		AddError(loginFlow.lastError());
		return false;
	}

	const Network::FLoginToken token = loginFlow.token();
	loginFlow.end();
	loginSession.disconnect();

	gameSession.connect(MakeUnique<Network::FTcpTransport>(), token.host, token.port, MakeUnique<Network::FGameCipher>());
	gameFlow.begin(gameSession, token);

	auto atCharacterSelect = [&]() { return (gameFlow.step() == Network::EGameServerStep::CharacterSelect) || (gameFlow.step() == Network::EGameServerStep::Failed); };
	if (!TestTrue(TEXT("The game server sends the character list"), pump(atCharacterSelect, 10.0) && (gameFlow.step() == Network::EGameServerStep::CharacterSelect)))
	{
		AddError(gameFlow.lastError());
		return false;
	}

	// The server's PacketEncryption decides this, and the test passes either way, so the log says which one ran.
	const bool encrypted = static_cast<Network::FGameCipher*>(gameSession.cipher())->isEnabled();
	AddInfo(encrypted ? TEXT("The game link is encrypted.") : TEXT("The game link runs in the clear."));

	auto findProbe = [&]()
	{
		return gameFlow.characters().FindByPredicate([](const Network::FCharacterListEntry& entry) { return entry.name == probeCharacter; });
	};

	const Network::FAppearance expected = probeAppearance();
	if (findProbe() == nullptr)
	{
		bool rejected = false;
		gameFlow.onCreateRejected.BindLambda([&rejected](Network::ECreateRejectReason reason) { rejected = true; });

		TestTrue(TEXT("The create request goes out"), gameFlow.createCharacter(probeCharacter, expected));
		pump([&]() { return rejected || (findProbe() != nullptr); }, 10.0);

		gameFlow.onCreateRejected.Unbind();
		if (!TestNotNull(TEXT("The probe character is created"), findProbe()))
		{
			return false;
		}
	}

	const Network::FCharacterListEntry probe = *findProbe();
	TestTrue(TEXT("The appearance block comes back from the server exactly as sent"), probe.appearance == expected.pack());

	gameFlow.selectCharacter(probe.objectId);
	const bool selected = pump([&]() { return (gameFlow.step() == Network::EGameServerStep::Selected) || (gameFlow.step() == Network::EGameServerStep::Failed); }, 10.0);
	if (!TestTrue(TEXT("The game server accepts the character"), selected && (gameFlow.step() == Network::EGameServerStep::Selected)))
	{
		AddError(gameFlow.lastError());
		return false;
	}

	int32 spawns = 0;
	TArray<Network::FCharacterSpawn> townspeople;
	worldFlow.onSpawned.BindLambda([&spawns, &townspeople](const Network::FCharacterSpawn& spawn)
	{
		spawns++;
		if (spawn.isNpc && !spawn.attackable)
		{
			townspeople.Add(spawn);
		}
	});
	TestTrue(TEXT("EnterWorld goes out"), worldFlow.begin(gameFlow));

	pump([&]() { return false; }, 4.0);
	TestTrue(FString::Printf(TEXT("The world sends what stands around the character (%d spawns)"), spawns), spawns > 0);
	TestTrue(TEXT("The link survives entering the world"), gameFlow.step() == Network::EGameServerStep::InWorld);

	// The run speed sits after the three equipment blocks, so a sensible value proves the whole read stayed aligned.
	const Network::FUserInfo& info = worldFlow.userInfo();
	TestTrue(FString::Printf(TEXT("UserInfo reads through to the run speed (level %d, %d HP, speed %d)"), info.level, info.maxHp, info.runSpeed), (info.level > 0) && (info.maxHp > 0) && (info.runSpeed > 50) && (info.runSpeed < 400));

	// A line said on the general channel comes back from the server to whoever said it, which checks both chat packets.
	FString heard;
	worldFlow.onChat.BindLambda([&heard](const Network::FChatMessage& message) { heard = message.text; });
	const FString line = TEXT("Probe line");
	TestTrue(TEXT("A chat line goes out"), worldFlow.say(line, Network::EChatChannel::General));
	pump([&]() { return heard == line; }, 5.0);
	worldFlow.onChat.Unbind();
	TestEqual(TEXT("The server says the chat line back"), heard, line);

	// Talking to a townsperson: stand next to the nearest ones, which keyboard mode lets the client do by reporting where it is, and click.
	// A townsperson with nothing to say, a guard for one, does not answer, so the nearest few are tried and one answer is enough.
	const FVector start = gameFlow.selected().location;
	townspeople.Sort([&start](const Network::FCharacterSpawn& left, const Network::FCharacterSpawn& right)
	{
		return FVector::Dist2D(left.location, start) < FVector::Dist2D(right.location, start);
	});

	TArray<Network::FNpcHtml> pages;
	worldFlow.onNpcHtml.BindLambda([&pages](const Network::FNpcHtml& message) { pages.Add(message); });
	TestTrue(FString::Printf(TEXT("A townsperson is in sight (%d)"), townspeople.Num()), townspeople.Num() > 0);

	bool answered = false;
	for (int32 index = 0; (index < FMath::Min(townspeople.Num(), 5)) && !answered; index++)
	{
		const Network::FCharacterSpawn& npc = townspeople[index];

		// A body's length in front of the townsperson, on the side the character came from.
		const FVector side = (start - npc.location).GetSafeNormal2D();
		const FVector spot = npc.location + (side * 120.0);
		worldFlow.setLocalState(spot, 0.0f, false);

		Network::FWriteBuffer walk;
		Network::GameServer::writeMoveToLocation(walk, spot + (side * -600.0), spot);
		gameSession.send(walk.bytes());
		pump([&]() { return false; }, 0.3);

		Network::FWriteBuffer place;
		Network::GameServer::writeValidatePosition(place, spot, 0.0f);
		gameSession.send(place.bytes());
		pump([&]() { return false; }, 0.3);

		pages.Reset();
		TestTrue(TEXT("The talk request goes out"), worldFlow.talkTo(npc.objectId));
		answered = pump([&]() { return pages.ContainsByPredicate([&npc](const Network::FNpcHtml& page) { return page.npcObjectId == npc.objectId; }); }, 3.0);
		if (!answered)
		{
			// A late answer from this one must not be taken for the next one's.
			pump([&]() { return false; }, 1.0);
			continue;
		}

		AddInfo(FString::Printf(TEXT("%s (%u) answered with %d characters of dialog."), *npc.name, npc.objectId, pages.Last().html.Len()));
		TestTrue(TEXT("The dialog is a page"), pages.Last().html.Contains(TEXT("<")));

		// The target is selected by now, so a second talk is one click, and the server answers it as well.
		pages.Reset();
		pump([&]() { return false; }, 0.5);
		TestTrue(TEXT("The second talk request goes out"), worldFlow.talkTo(npc.objectId));
		TestTrue(TEXT("A townsperson who is already selected answers the single click"), pump([&]() { return pages.ContainsByPredicate([&npc](const Network::FNpcHtml& page) { return page.npcObjectId == npc.objectId; }); }, 3.0));
	}

	TestTrue(TEXT("A townsperson answers the click with a dialog"), answered);
	worldFlow.onNpcHtml.Unbind();

	worldFlow.end();
	gameFlow.end();
	gameSession.disconnect();
	return true;
}

#endif
