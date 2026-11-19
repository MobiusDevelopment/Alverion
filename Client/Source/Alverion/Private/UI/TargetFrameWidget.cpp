#include "UI/TargetFrameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UIStyle.h"
#include "World/WorldGameMode.h"

namespace
{
	constexpr float targetFrameTopMargin = 24.0f;
	constexpr float targetFrameBarWidth = 300.0f;
	constexpr float targetFrameBarHeight = 14.0f;
	constexpr int32 targetFrameNameFontSize = 15;
	constexpr FLinearColor targetFrameBarBackground = FLinearColor(0.02f, 0.018f, 0.016f, 0.85f);
	constexpr FLinearColor targetFrameBarColour = FLinearColor(0.72f, 0.12f, 0.1f, 1.0f);
}

TSharedRef<SWidget> UTargetFrameWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UOverlaySlot* columnSlot = root->AddChildToOverlay(column);
		columnSlot->SetHorizontalAlignment(HAlign_Center);
		columnSlot->SetVerticalAlignment(VAlign_Top);
		columnSlot->SetPadding(FMargin(0.0f, targetFrameTopMargin, 0.0f, 0.0f));

		_name = UIStyle::makeLabel(*WidgetTree, FText::GetEmpty(), targetFrameNameFontSize, UIStyle::labelColor);
		_name->SetJustification(ETextJustify::Center);
		UVerticalBoxSlot* nameSlot = column->AddChildToVerticalBox(_name);
		nameSlot->SetHorizontalAlignment(HAlign_Center);
		nameSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));

		USizeBox* barBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		barBox->SetWidthOverride(targetFrameBarWidth);
		barBox->SetHeightOverride(targetFrameBarHeight);

		_bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		FProgressBarStyle style = _bar->GetWidgetStyle();
		style.BackgroundImage = UIStyle::flatBrush(targetFrameBarBackground);
		style.FillImage = UIStyle::flatBrush(FLinearColor::White);
		_bar->SetWidgetStyle(style);
		_bar->SetFillColorAndOpacity(targetFrameBarColour);
		barBox->AddChild(_bar);
		column->AddChildToVerticalBox(barBox);

		_frame = column;
		_frame->SetVisibility(ESlateVisibility::Collapsed);
		WidgetTree->RootWidget = root;

		// Seen, never clicked: the world keeps the mouse.
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	return Super::RebuildWidget();
}

void UTargetFrameWidget::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
	Super::NativeTick(geometry, deltaSeconds);

	UGameInstance* gameInstance = GetGameInstance();
	UServerConnectionSubsystem* link = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	const UWorld* world = GetWorld();
	const AWorldGameMode* mode = (world != nullptr) ? world->GetAuthGameMode<AWorldGameMode>() : nullptr;
	if ((link == nullptr) || (mode == nullptr) || (_frame == nullptr))
	{
		return;
	}

	const uint32 target = link->world().targetObjectId();
	int32 current = 0;
	int32 maximum = 0;
	const bool shown = (target != 0) && link->world().healthOf(target, current, maximum);
	_frame->SetVisibility(shown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!shown || ((target == _shownTarget) && (current == _shownCurrent) && (maximum == _shownMaximum)))
	{
		return;
	}

	if (target != _shownTarget)
	{
		_name->SetText(FText::FromString(mode->nameOf(target)));
	}

	_shownTarget = target;
	_shownCurrent = current;
	_shownMaximum = maximum;
	_bar->SetPercent(FMath::Clamp(static_cast<float>(current) / static_cast<float>(maximum), 0.0f, 1.0f));
}
