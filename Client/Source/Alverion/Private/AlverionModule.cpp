#include "Internationalization/StringTableRegistry.h"
#include "Items/ItemCatalog.h"
#include "Boot/LoadingScreen.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "MoviePlayer.h"

namespace client
{
	/**
	 * The loading screen, shown by the movie player during a map load, armed before each load because the movie player shows what it was last given.
	 * A map load blocks the game thread, so only the movie player, rendering on its own thread, can draw then.
	 * Off unless a run asks for it with -loadingscreen: in the old client eight of fifteen runs with it up died on a null read under RenderGraphExecute drawing Slate, and none with it off.
	 */
	void armLoadingScreen()
	{
		if (!IsMoviePlayerEnabled() || !FParse::Param(FCommandLine::Get(), TEXT("loadingscreen")))
		{
			return;
		}

		// Not one frame past the map load: keeping the movie player's own renderer up once the world renders is two renderers drawing, which is what crashed.
		FLoadingScreenAttributes attributes;
		attributes.MinimumLoadingScreenDisplayTime = -1.0f;
		attributes.bAutoCompleteWhenLoadingCompletes = true;
		attributes.bWaitForManualStop = false;
		attributes.bAllowEngineTick = false;
		attributes.WidgetLoadingScreen = makeLoadingScreen();

		GetMoviePlayer()->SetupLoadingScreen(attributes);
	}
}

/** Registers the menus' string table before any screen asks it for text, and arms the loading screen for every map load. */
class FAlverionModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

		// A CSV rather than a string table asset, so anyone can add or change a line as text.
		// The namespace is what the translations are keyed under, so it must stay ST_Game for them to match.
		LOCTABLE_FROMFILE_GAME("ST_Game", "ST_Game", "Localization/StringTables/ST_Game.csv");
		ItemCatalog::registerTable();

		// The first map is loaded by the engine before anything of ours runs, so it is armed here.
		client::armLoadingScreen();

		_preLoadMap = FCoreUObjectDelegates::PreLoadMap.AddStatic(&FAlverionModule::handlePreLoadMap);
	}

	virtual void ShutdownModule() override
	{
		FCoreUObjectDelegates::PreLoadMap.Remove(_preLoadMap);

		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	/** Every map after the first. */
	static void handlePreLoadMap(const FString& mapName)
	{
		client::armLoadingScreen();
	}

	FDelegateHandle _preLoadMap;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAlverionModule, Alverion, "Alverion");
