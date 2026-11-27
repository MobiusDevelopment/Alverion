#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "MapWidget.generated.h"

class UCanvasPanelSlot;
class UInputAction;
class UWidget;

/**
 * The map of the continent, opened over the running world by the map key and closed by it or Escape.
 * The picture is the continent itself, filmed straight down and north up, and a marker shows where the player stands and which way they face.
 * The mouse wheel zooms toward the cursor and dragging with the left button moves the picture; the marker keeps its size at any zoom.
 */
UCLASS()
class ALVERION_API UMapWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	/** Escape, or the map key wherever it was rebound, closes the map: CommonUI holds the game's keys back while it is up. */
	virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

	virtual FReply NativeOnMouseWheel(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& captureLostEvent) override;

private:
	/** Puts the marker where the player stands and turns it the way they face. */
	void placeMarker();

	/** Keeps the picture covering the frame, then scales and moves it to the current zoom and pan. */
	void applyView();

	/** The picture, scaled and moved by the zoom and pan. */
	UPROPERTY()
	TObjectPtr<UWidget> _picture;

	/** The clipped frame the picture shows through; its geometry turns screen positions into map positions. */
	UPROPERTY()
	TObjectPtr<UWidget> _frame;

	/** 1 shows the whole map, larger values magnify it. */
	float _zoom = 1.0f;

	/** Where the picture's top left corner sits in the frame, in the map's design units. */
	FVector2D _offset = FVector2D::ZeroVector;

	bool _dragging = false;
	FVector2D _dragLast = FVector2D::ZeroVector;

	UPROPERTY()
	TObjectPtr<UWidget> _marker;

	UPROPERTY()
	TObjectPtr<UCanvasPanelSlot> _markerSlot;

	UPROPERTY()
	TObjectPtr<UInputAction> _mapAction;
};
