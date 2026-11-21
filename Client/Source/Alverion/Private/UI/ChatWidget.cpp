#include "UI/ChatWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "ServerConnectionSubsystem.h"
#include "UI/UIStyle.h"

namespace
{
	constexpr float chatWidth = 460.0f;
	constexpr float chatLogHeight = 180.0f;
	constexpr float chatSideMargin = 24.0f;

	// Above the action bar's row and above the line to type on, which opens just under the log.
	constexpr float chatBottomMargin = 126.0f;

	constexpr int32 chatFontSize = 14;
	constexpr int32 chatKeptLines = 100;
	constexpr FLinearColor chatLogFill = FLinearColor(0.0f, 0.0f, 0.0f, 0.25f);
	constexpr FLinearColor chatShoutColor = FLinearColor(1.0f, 0.62f, 0.25f, 1.0f);
	constexpr FLinearColor chatShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.8f);

	UServerConnectionSubsystem* chatConnectionOf(const UUserWidget& widget)
	{
		UGameInstance* gameInstance = widget.GetGameInstance();
		return (gameInstance != nullptr) ? gameInstance->GetSubsystem<UServerConnectionSubsystem>() : nullptr;
	}

	// Shouts stand out in orange, everything said nearby is plain, and the channels this client only listens to are muted.
	FLinearColor chatColourOf(int32 channel)
	{
		if (channel == static_cast<int32>(Network::EChatChannel::Shout))
		{
			return chatShoutColor;
		}

		if (channel == static_cast<int32>(Network::EChatChannel::General))
		{
			return UIStyle::labelColor;
		}

		return UIStyle::mutedColor;
	}
}

TSharedRef<SWidget> UChatWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		USizeBox* logBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		logBox->SetWidthOverride(chatWidth);
		logBox->SetHeightOverride(chatLogHeight);

		UOverlaySlot* logSlot = root->AddChildToOverlay(logBox);
		logSlot->SetHorizontalAlignment(HAlign_Left);
		logSlot->SetVerticalAlignment(VAlign_Bottom);
		logSlot->SetPadding(FMargin(chatSideMargin, 0.0f, 0.0f, chatBottomMargin));

		UBorder* logPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		logPanel->SetBrush(UIStyle::flatBrush(chatLogFill));
		logPanel->SetPadding(FMargin(8.0f, 6.0f));
		logBox->AddChild(logPanel);

		_log = UIStyle::makeScrollBox(*WidgetTree);
		_log->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		logPanel->SetContent(_log);

		WidgetTree->RootWidget = root;

		// Read, never clicked, so clicks pass to the world.
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	return Super::RebuildWidget();
}

void UChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UServerConnectionSubsystem* connection = chatConnectionOf(*this);
	if (connection != nullptr)
	{
		connection->world().onChat.BindUObject(this, &UChatWidget::addMessage);
	}
}

void UChatWidget::NativeDestruct()
{
	UServerConnectionSubsystem* connection = chatConnectionOf(*this);
	if ((connection != nullptr) && connection->world().onChat.IsBoundToObject(this))
	{
		connection->world().onChat.Unbind();
	}

	Super::NativeDestruct();
}

void UChatWidget::addMessage(const Network::FChatMessage& message)
{
	if (_log == nullptr)
	{
		return;
	}

	const FString shown = message.senderName.IsEmpty() ? message.text : FString::Printf(TEXT("%s: %s"), *message.senderName, *message.text);
	UTextBlock* line = UIStyle::makeLabel(*WidgetTree, FText::FromString(shown), chatFontSize, chatColourOf(message.channel));
	line->SetAutoWrapText(true);
	line->SetShadowOffset(FVector2D(1.0, 1.0));
	line->SetShadowColorAndOpacity(chatShadowColor);
	_log->AddChild(line);

	// The oldest lines go once there are enough to read back through.
	while (_log->GetChildrenCount() > chatKeptLines)
	{
		_log->RemoveChildAt(0);
	}

	_log->ScrollToEnd();
}
