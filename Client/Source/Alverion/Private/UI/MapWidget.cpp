#include "UI/MapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "UI/LocalizedText.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"

namespace
{
	const TCHAR* const mapActionPath = TEXT("/Game/Input/Actions/IA_Map.IA_Map");

	/**
	 * L_Continent filmed straight down, north up, east to the right: the terrain, buildings, trees and ships, but no characters,
	 * which only exist once the server spawns them. 16 orthographic tiles stitched into one 8192 square picture.
	 * The picture covers exactly this square of the world, in centimetres; bake again and these change with it.
	 */
	const TCHAR* const mapTexturePath = TEXT("/Game/UI/Map/T_ContinentMap.T_ContinentMap");
	constexpr double mapWorldMinX = -508010.0;
	constexpr double mapWorldMinY = 815990.0;
	constexpr double mapWorldMaxX = -255990.0;
	constexpr double mapWorldMaxY = 1068010.0;

	// The picture's size on screen before it is shrunk to fit; square, like the picture.
	constexpr float mapDesignWidth = 1000.0f;
	constexpr float mapDesignHeight = mapDesignWidth;

	// The wheel magnifies by this much a notch, up to the most the picture's detail holds.
	constexpr float mapZoomStep = 1.25f;
	constexpr float mapZoomMax = 8.0f;

	constexpr FLinearColor mapBackdropColor = FLinearColor(0.01f, 0.01f, 0.015f, 0.55f);
	constexpr FLinearColor mapPanelFill = FLinearColor(0.035f, 0.03f, 0.028f, 0.94f);
	constexpr FLinearColor mapHeadingColor = FLinearColor(0.86f, 0.68f, 0.36f, 1.0f);
	constexpr float mapPanelRadius = 8.0f;
	constexpr float mapPanelPadding = 16.0f;
	constexpr int32 mapHeadingFontSize = 20;

	// The marker: a gold arrow drawn pointing right at no turn, which is east, the way a yaw of zero faces.
	const TCHAR* const mapMarkerTexturePath = TEXT("/Game/UI/Map/T_MapMarkerArrow.T_MapMarkerArrow");
	constexpr float mapMarkerSize = 30.0f;
}

TSharedRef<SWidget> UMapWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush backdropBrush;
		backdropBrush.TintColor = FSlateColor(mapBackdropColor);
		backdrop->SetBrush(backdropBrush);

		UOverlaySlot* backdropSlot = root->AddChildToOverlay(backdrop);
		backdropSlot->SetHorizontalAlignment(HAlign_Fill);
		backdropSlot->SetVerticalAlignment(VAlign_Fill);

		// Shrunk to fit a small window, never grown past the picture's design size.
		UScaleBox* fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
		fit->SetStretch(EStretch::ScaleToFit);
		fit->SetStretchDirection(EStretchDirection::DownOnly);

		UOverlaySlot* fitSlot = root->AddChildToOverlay(fit);
		fitSlot->SetHorizontalAlignment(HAlign_Center);
		fitSlot->SetVerticalAlignment(VAlign_Center);
		fitSlot->SetPadding(FMargin(24.0f));

		UBorder* panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		panel->SetBrush(UIStyle::stateBrush(mapPanelFill, UIStyle::outline, mapPanelRadius));
		panel->SetPadding(FMargin(mapPanelPadding));
		fit->AddChild(panel);

		UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		panel->SetContent(column);

		UTextBlock* heading = UIStyle::makeLabel(*WidgetTree, LocalizedText::get(TEXT("map.title")), mapHeadingFontSize, mapHeadingColor);
		UVerticalBoxSlot* headingSlot = column->AddChildToVerticalBox(heading);
		headingSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

		// Clipped, so a magnified picture shows only the part inside the frame.
		USizeBox* frame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		frame->SetWidthOverride(mapDesignWidth);
		frame->SetHeightOverride(mapDesignHeight);
		frame->SetClipping(EWidgetClipping::ClipToBounds);
		column->AddChildToVerticalBox(frame);
		_frame = frame;

		UOverlay* layers = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		frame->AddChild(layers);

		UImage* picture = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		UTexture2D* texture = LoadObject<UTexture2D>(nullptr, mapTexturePath);
		if (texture != nullptr)
		{
			picture->SetBrushFromTexture(texture, false);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MapWidget could not load %s, so the map is blank."), mapTexturePath);
		}

		UOverlaySlot* pictureSlot = layers->AddChildToOverlay(picture);
		pictureSlot->SetHorizontalAlignment(HAlign_Fill);
		pictureSlot->SetVerticalAlignment(VAlign_Fill);

		// Zoom scales the picture from its top left corner, and pan moves that corner.
		picture->SetRenderTransformPivot(FVector2D::ZeroVector);
		_picture = picture;

		// The marker rides an unscaled canvas the frame's size, placed through the zoom and pan, so it keeps its size.
		UCanvasPanel* canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		UOverlaySlot* canvasSlot = layers->AddChildToOverlay(canvas);
		canvasSlot->SetHorizontalAlignment(HAlign_Fill);
		canvasSlot->SetVerticalAlignment(VAlign_Fill);

		UImage* marker = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		UTexture2D* markerTexture = LoadObject<UTexture2D>(nullptr, mapMarkerTexturePath);
		if (markerTexture != nullptr)
		{
			marker->SetBrushFromTexture(markerTexture, false);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MapWidget could not load %s, so the player marker is blank."), mapMarkerTexturePath);
		}

		_marker = marker;
		_markerSlot = canvas->AddChildToCanvas(marker);
		_markerSlot->SetSize(FVector2D(mapMarkerSize, mapMarkerSize));
		_markerSlot->SetAlignment(FVector2D(0.5, 0.5));

		WidgetTree->RootWidget = root;

		// Focusable, so the keys reach it once CommonUI puts the focus here.
		SetIsFocusable(true);
		_mapAction = LoadObject<UInputAction>(nullptr, mapActionPath);
	}

	return Super::RebuildWidget();
}

void UMapWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	placeMarker();
}

void UMapWidget::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
	Super::NativeTick(geometry, deltaSeconds);

	// Auto run carries on under the map, so the marker follows.
	placeMarker();
}

void UMapWidget::placeMarker()
{
	const APawn* pawn = GetOwningPlayerPawn();
	if ((pawn == nullptr) || (_markerSlot == nullptr) || (_marker == nullptr))
	{
		return;
	}

	// East runs across the picture and south down it. A player off the mapped stretch stays pinned to its edge.
	const FVector location = pawn->GetActorLocation();
	const double across = FMath::Clamp((location.X - mapWorldMinX) / (mapWorldMaxX - mapWorldMinX), 0.0, 1.0);
	const double down = FMath::Clamp((location.Y - mapWorldMinY) / (mapWorldMaxY - mapWorldMinY), 0.0, 1.0);
	_markerSlot->SetPosition(_offset + FVector2D(across * mapDesignWidth, down * mapDesignHeight) * _zoom);

	// Yaw turns clockwise seen from above, and so does a widget's angle, with both at zero facing east.
	_marker->SetRenderTransformAngle(pawn->GetActorRotation().Yaw);
}

UWidget* UMapWidget::NativeGetDesiredFocusTarget() const
{
	// Nothing on the map takes input yet, so the map itself holds the focus the keys arrive through.
	return const_cast<UMapWidget*>(this);
}

FReply UMapWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
	const FKey key = keyEvent.GetKey();
	if (key == EKeys::Escape)
	{
		UGameInstance* gameInstance = GetGameInstance();
		UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
		if ((ui == nullptr) || ui->acceptEscape(keyEvent.IsRepeat()))
		{
			DeactivateWidget();
		}

		return FReply::Handled();
	}

	ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* input = (localPlayer != nullptr) ? localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!keyEvent.IsRepeat() && (input != nullptr) && (_mapAction != nullptr) && input->QueryKeysMappedToAction(_mapAction).Contains(key))
	{
		DeactivateWidget();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(geometry, keyEvent);
}

FReply UMapWidget::NativeOnMouseWheel(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if ((_frame == nullptr) || (_picture == nullptr))
	{
		return Super::NativeOnMouseWheel(geometry, mouseEvent);
	}

	// The map point under the cursor stays under it, so the wheel zooms toward whatever it points at.
	const FVector2D local = _frame->GetCachedGeometry().AbsoluteToLocal(mouseEvent.GetScreenSpacePosition());
	const FVector2D cursor(FMath::Clamp(local.X, 0.0, static_cast<double>(mapDesignWidth)), FMath::Clamp(local.Y, 0.0, static_cast<double>(mapDesignHeight)));
	const float zoom = FMath::Clamp(_zoom * FMath::Pow(mapZoomStep, mouseEvent.GetWheelDelta()), 1.0f, mapZoomMax);
	_offset = cursor - (cursor - _offset) * (zoom / _zoom);
	_zoom = zoom;
	applyView();
	return FReply::Handled();
}

FReply UMapWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if ((mouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) || (_frame == nullptr))
	{
		return Super::NativeOnMouseButtonDown(geometry, mouseEvent);
	}

	_dragging = true;
	_dragLast = mouseEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UMapWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if (!_dragging || (_frame == nullptr))
	{
		return Super::NativeOnMouseMove(geometry, mouseEvent);
	}

	// Measured in the frame's own units, so the picture follows the cursor however much the panel was shrunk to fit.
	const FGeometry& frameGeometry = _frame->GetCachedGeometry();
	_offset += frameGeometry.AbsoluteToLocal(mouseEvent.GetScreenSpacePosition()) - frameGeometry.AbsoluteToLocal(_dragLast);
	_dragLast = mouseEvent.GetScreenSpacePosition();
	applyView();
	return FReply::Handled();
}

FReply UMapWidget::NativeOnMouseButtonUp(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
	if (!_dragging || (mouseEvent.GetEffectingButton() != EKeys::LeftMouseButton))
	{
		return Super::NativeOnMouseButtonUp(geometry, mouseEvent);
	}

	_dragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

void UMapWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& captureLostEvent)
{
	Super::NativeOnMouseCaptureLost(captureLostEvent);

	_dragging = false;
}

void UMapWidget::applyView()
{
	// The picture never shows a gap: at any zoom its edges stay at or past the frame's.
	_offset.X = FMath::Clamp(_offset.X, static_cast<double>(mapDesignWidth * (1.0f - _zoom)), 0.0);
	_offset.Y = FMath::Clamp(_offset.Y, static_cast<double>(mapDesignHeight * (1.0f - _zoom)), 0.0);

	FWidgetTransform transform;
	transform.Translation = _offset;
	transform.Scale = FVector2D(_zoom, _zoom);
	_picture->SetRenderTransform(transform);

	placeMarker();
}
