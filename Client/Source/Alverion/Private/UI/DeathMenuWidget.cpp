#include "UI/DeathMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/CommonUIInputTypes.h"
#include "UI/LocalizedText.h"
#include "UI/UIStyle.h"

namespace
{
	// Narrower than a question, since it says one thing and offers one way on.
	constexpr float deathMenuWidth = 480.0f;
	constexpr float deathMenuPadding = 24.0f;
	constexpr int32 deathMenuTitleFontSize = 30;

	// The confirm dialog's panel, so the two read as the same family.
	const FLinearColor deathMenuPanelColor = FLinearColor::FromSRGBColor(FColor(84, 62, 47));
}

void UDeathMenuWidget::setup(TFunction<void()> onRespawn)
{
	_onRespawn = MoveTemp(onRespawn);
}

TSharedRef<SWidget> UDeathMenuWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		UBorder* scrim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		scrim->SetBrushColor(UIStyle::scrimColor);
		scrim->SetHorizontalAlignment(HAlign_Center);
		scrim->SetVerticalAlignment(VAlign_Center);

		USizeBox* panel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		panel->SetWidthOverride(deathMenuWidth);

		UBorder* background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		background->SetBrush(UIStyle::roundedBrush(deathMenuPanelColor));
		background->SetPadding(FMargin(deathMenuPadding));

		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		UTextBlock* title = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("combat.died")), deathMenuTitleFontSize, UIStyle::labelColor);
		title->SetJustification(ETextJustify::Center);
		column->AddChildToVerticalBox(title)->SetHorizontalAlignment(HAlign_Center);

		_respawnButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		_respawnButton->SetStyle(UIStyle::buttonStyle());
		UTextBlock* label = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("combat.respawn")), UIStyle::bodyFontSize, UIStyle::labelColor);
		label->SetJustification(ETextJustify::Center);
		_respawnButton->AddChild(label);

		UVerticalBoxSlot* buttonSlot = column->AddChildToVerticalBox(_respawnButton);
		buttonSlot->SetHorizontalAlignment(HAlign_Center);
		buttonSlot->SetPadding(FMargin(0.0f, deathMenuPadding, 0.0f, 0.0f));

		background->AddChild(column);
		panel->AddChild(background);
		scrim->AddChild(panel);
		WidgetTree->RootWidget = scrim;
	}

	return Super::RebuildWidget();
}

void UDeathMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Unique, because the stack can hand back the same instance for the next death.
	if (_respawnButton != nullptr)
	{
		_respawnButton->OnClicked.AddUniqueDynamic(this, &UDeathMenuWidget::handleRespawnClicked);
	}

	SetIsFocusable(true);
}

TOptional<FUIInputConfig> UDeathMenuWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

bool UDeathMenuWidget::NativeOnHandleBackAction()
{
	// Handled, and the menu stays: Respawn is the only way on.
	return true;
}

void UDeathMenuWidget::handleRespawnClicked()
{
	// Taken before closing, since the menu may be handed out again by the time the server answers.
	TFunction<void()> respawn = MoveTemp(_onRespawn);
	DeactivateWidget();
	if (respawn)
	{
		respawn();
	}
}
