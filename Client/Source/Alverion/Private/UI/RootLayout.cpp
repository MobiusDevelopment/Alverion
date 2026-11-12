#include "UI/RootLayout.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "UI/ConfirmDialog.h"
#include "Engine/GameInstance.h"
#include "UI/UISubsystem.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace
{
	// How long a screen takes to cross fade into the next one.
	//
	// CommonUI defaults this to 0.4 and nothing here used to set it, which was long enough to see.
	// Both screens sit near transparent through the middle of a fade.
	// The front end fills are very dark and their outlines lighter.
	// Over sunlit sand the fills wash out first, so a screen briefly reads as empty outlines.
	//
	// Shortened rather than removed, since zero was ruled out.
	// A cut is worse than a quick fade anyway, the camera still blending between stops underneath.
	constexpr float layerTransitionSeconds = 0.15f;

	// How long the black over everything takes to clear once the map is running, since a loaded map is not yet a dressed one.
	constexpr float coverFadeSeconds = 1.0f;

	// One full screen stack. The order these are added is the z order of the layers.
	UCommonActivatableWidgetStack* addLayerStack(UWidgetTree& tree, UOverlay& layers)
	{
		UCommonActivatableWidgetStack* stack = tree.ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass());
		stack->SetTransitionDuration(layerTransitionSeconds);

		// The screens on a stack take the mouse, the empty stack around them does not, so the action bar beneath can take a dropped item.
		stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UOverlaySlot* slot = layers.AddChildToOverlay(stack);
		slot->SetHorizontalAlignment(HAlign_Fill);
		slot->SetVerticalAlignment(VAlign_Fill);

		return stack;
	}
}

TSharedRef<SWidget> URootLayout::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// Four stacks over each other, bottom to top: game, its menu, the front end menus, then modals.
		UOverlay* layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		gameLayer = addLayerStack(*WidgetTree, *layers);
		gameMenuLayer = addLayerStack(*WidgetTree, *layers);
		menuLayer = addLayerStack(*WidgetTree, *layers);
		modalLayer = addLayerStack(*WidgetTree, *layers);

		// Last, so it is over all four, and deaf, so it never eats the click it is covering.
		FSlateBrush coverBrush;
		coverBrush.TintColor = FSlateColor(FLinearColor::Black);

		_cover = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		_cover->SetBrush(coverBrush);
		_cover->SetVisibility(ESlateVisibility::HitTestInvisible);

		UOverlaySlot* coverSlot = layers->AddChildToOverlay(_cover);
		coverSlot->SetHorizontalAlignment(HAlign_Fill);
		coverSlot->SetVerticalAlignment(VAlign_Fill);

		WidgetTree->RootWidget = layers;
	}

	return Super::RebuildWidget();
}

// Fades rather than waits, since a second of black that then cuts is a cut.
// Real time rather than world time, so a hitch on the first frames does not stretch it.
void URootLayout::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
	Super::NativeTick(geometry, deltaSeconds);

	if ((_cover == nullptr) || (_coverAlpha <= 0.0f))
	{
		return;
	}

	_coverAlpha -= deltaSeconds / coverFadeSeconds;

	if (_coverAlpha <= 0.0f)
	{
		_coverAlpha = 0.0f;

		// Collapsed rather than left at zero opacity, so it stops being drawn at all.
		_cover->SetVisibility(ESlateVisibility::Collapsed);

		return;
	}

	_cover->SetRenderOpacity(_coverAlpha);
}

void URootLayout::NativeConstruct()
{
	Super::NativeConstruct();

	UGameInstance* gameInstance = GetGameInstance();
	if (gameInstance == nullptr)
	{
		return;
	}

	UUISubsystem* subsystem = gameInstance->GetSubsystem<UUISubsystem>();
	if (subsystem == nullptr)
	{
		return;
	}

	// An unbound stack means the widget blueprint is missing that layer, which registerLayer reports.
	subsystem->registerLayer(EUILayer::Game, gameLayer);
	subsystem->registerLayer(EUILayer::GameMenu, gameMenuLayer);
	subsystem->registerLayer(EUILayer::Menu, menuLayer);
	subsystem->registerLayer(EUILayer::Modal, modalLayer);

	// The C++ class itself, because there is no widget blueprint for the dialog any more.
	// A TSubclassOf here only ever existed so one asset could name another.
	subsystem->registerConfirmDialogClass(UConfirmDialog::StaticClass());
}

void URootLayout::NativeDestruct()
{
	UGameInstance* gameInstance = GetGameInstance();
	if (gameInstance != nullptr)
	{
		UUISubsystem* subsystem = gameInstance->GetSubsystem<UUISubsystem>();
		if (subsystem != nullptr)
		{
			// The subsystem outlives this widget, so leaving dead stacks behind would strand later pushes.
			subsystem->unregisterLayer(EUILayer::Game);
			subsystem->unregisterLayer(EUILayer::GameMenu);
			subsystem->unregisterLayer(EUILayer::Menu);
			subsystem->unregisterLayer(EUILayer::Modal);
		}
	}

	Super::NativeDestruct();
}
