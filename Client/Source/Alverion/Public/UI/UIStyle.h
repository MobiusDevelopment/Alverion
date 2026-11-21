#pragma once

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "CoreMinimal.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

// The front end's look in one place, so a screen styled from C++ cannot drift from the ones beside it.
// Header only and constexpr on purpose,
// because constants defined in one cpp and read from another depend on initialization order.
namespace UIStyle
{
	inline constexpr FLinearColor labelColor = FLinearColor(0.92f, 0.9f, 0.84f, 1.0f);
	inline constexpr FLinearColor fillNormal = FLinearColor(0.115f, 0.062f, 0.032f, 1.0f);
	inline constexpr FLinearColor fillHovered = FLinearColor(0.2f, 0.115f, 0.058f, 1.0f);
	inline constexpr FLinearColor fillPressed = FLinearColor(0.075f, 0.04f, 0.02f, 1.0f);
	inline constexpr FLinearColor outline = FLinearColor(0.36f, 0.24f, 0.13f, 1.0f);

	// The front end's buttons brighten their outline on hover and sink it on press, not just the fill.
	// Read out of the widget blueprints with the DumpWidget commandlet, where all three screens agreed.
	inline constexpr FLinearColor outlineHovered = FLinearColor(0.62f, 0.45f, 0.22f, 1.0f);
	inline constexpr FLinearColor outlinePressed = FLinearColor(0.3f, 0.2f, 0.11f, 1.0f);

	// For text that is present but says nothing yet, such as a key binding with no key.
	inline constexpr FLinearColor mutedColor = FLinearColor(0.55f, 0.5f, 0.45f, 1.0f);

	// Dark enough to sink whatever sits behind a modal without hiding it.
	inline constexpr FLinearColor scrimColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.6f);

	inline constexpr int32 bodyFontSize = 22;

	// The screens that list things share a shape, so they share its numbers.
	// These were copied into four files.
	// A unity build eventually put two copies in one translation unit and refused them,
	// which is the loudest a duplicate ever gets.
	inline constexpr int32 titleFontSize = 34;
	inline constexpr float panelWidth = 720.0f;
	inline constexpr float rowSpacing = 6.0f;

	// Keeps a row's ends off its rounded edges.
	inline constexpr float rowPadding = 16.0f;

	// Named because the confirm dialog rounds a brush the blueprint handed it.
	// A second literal would be a second thing to remember.
	inline constexpr float cornerRadius = 10.0f;

	// The studio mark is text until there is a logo to show, on a black card where the full screen logo picture sat.
	// Shared by the intro map and the front end's intro, so the two never drift apart.
	inline const TCHAR* const studioName = TEXT("Mobius Development");
	inline constexpr int32 studioFontSize = 96;
	inline constexpr int32 studioLetterSpacing = 120;
	inline constexpr FLinearColor cardColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	// Every front end screen clicks with the same sound,
	// and three copies of the path already collided once in a unity build.
	inline const TCHAR* const clickSoundPath = TEXT("/Game/Audio/UI/Click.Click");

	/** A Unicode block, inclusive at both ends. */
	inline FInt32Range codepoints(int32 first, int32 last)
	{
		return FInt32Range(FInt32Range::BoundsType::Inclusive(first), FInt32Range::BoundsType::Inclusive(last));
	}

	/**
	 * OSerif covers Latin only, so the other scripts the menus are translated into come from the Noto Serif family (SIL Open Font License).
	 * Greek and Cyrillic from Noto Serif; kana from the Japanese face and Hangul from the Korean one, whatever the language.
	 * The Han characters the three share are drawn differently in each, so they follow the language: Japanese, Korean or Simplified Chinese.
	 * Built once and kept, because the font cache keys on the composite's address and its faces must outlive every widget.
	 */
	inline TSharedPtr<const FCompositeFont> compositeFont()
	{
		static TSharedPtr<const FCompositeFont> composite;
		if (composite.IsValid())
		{
			return composite;
		}

		UObject* latin = LoadObject<UObject>(nullptr, TEXT("/Game/Fonts/OSerif-Regular.OSerif-Regular"));
		UObject* greekAndCyrillic = LoadObject<UObject>(nullptr, TEXT("/Game/Fonts/NotoSerif-Regular.NotoSerif-Regular"));
		UObject* japanese = LoadObject<UObject>(nullptr, TEXT("/Game/Fonts/NotoSerifJP-Regular.NotoSerifJP-Regular"));
		UObject* korean = LoadObject<UObject>(nullptr, TEXT("/Game/Fonts/NotoSerifKR-Regular.NotoSerifKR-Regular"));
		UObject* chinese = LoadObject<UObject>(nullptr, TEXT("/Game/Fonts/NotoSerifSC-Regular.NotoSerifSC-Regular"));
		if ((latin == nullptr) || (greekAndCyrillic == nullptr) || (japanese == nullptr) || (korean == nullptr) || (chinese == nullptr))
		{
			return nullptr;
		}

		// Nothing else holds these faces once the composite is made from them.
		for (UObject* face : { latin, greekAndCyrillic, japanese, korean, chinese })
		{
			face->AddToRoot();
		}

		const FName regular(TEXT("Regular"));
		TSharedRef<FCompositeFont> font = MakeShared<FCompositeFont>();
		font->DefaultTypeface.Fonts.Emplace_GetRef(regular).Font = FFontData(latin);

		const TArray<FInt32Range> sharedHan = { codepoints(0x3000, 0x303F), codepoints(0x3400, 0x4DBF), codepoints(0x4E00, 0x9FFF), codepoints(0xF900, 0xFAFF), codepoints(0xFF00, 0xFFEF) };
		const TPair<UObject*, const TCHAR*> hanByLanguage[] = { { japanese, TEXT("ja") }, { korean, TEXT("ko") }, { chinese, TEXT("zh") } };
		for (const TPair<UObject*, const TCHAR*>& language : hanByLanguage)
		{
			FCompositeSubFont& han = font->SubTypefaces.AddDefaulted_GetRef();
			han.Typeface.Fonts.Emplace_GetRef(regular).Font = FFontData(language.Key);
			han.CharacterRanges = sharedHan;
			han.Cultures = language.Value;
		}

		FCompositeSubFont& greekAndCyrillicFont = font->SubTypefaces.AddDefaulted_GetRef();
		greekAndCyrillicFont.Typeface.Fonts.Emplace_GetRef(regular).Font = FFontData(greekAndCyrillic);
		greekAndCyrillicFont.CharacterRanges = { codepoints(0x0370, 0x04FF), codepoints(0x1F00, 0x1FFF) };

		FCompositeSubFont& kana = font->SubTypefaces.AddDefaulted_GetRef();
		kana.Typeface.Fonts.Emplace_GetRef(regular).Font = FFontData(japanese);
		kana.CharacterRanges = { codepoints(0x3040, 0x30FF), codepoints(0x31F0, 0x31FF) };

		FCompositeSubFont& hangul = font->SubTypefaces.AddDefaulted_GetRef();
		hangul.Typeface.Fonts.Emplace_GetRef(regular).Font = FFontData(korean);
		hangul.CharacterRanges = { codepoints(0x1100, 0x11FF), codepoints(0x3130, 0x318F), codepoints(0xAC00, 0xD7AF) };

		// Han in any other language, a Chinese name in an English menu for one.
		font->FallbackTypeface.Typeface.Fonts.Emplace_GetRef(regular).Font = FFontData(chinese);

		composite = font;
		return composite;
	}

	inline FSlateFontInfo font(int32 size)
	{
		const TSharedPtr<const FCompositeFont> composite = compositeFont();
		if (!composite.IsValid())
		{
			return FCoreStyle::GetDefaultFontStyle("Regular", size);
		}

		return FSlateFontInfo(composite, size);
	}

	// The same font with a dark rim around every glyph.
	//
	// For text that stands on the scene rather than on a panel.
	// The creation screen's row names sit over a lit scene, and muted grey on white is unreadable.
	// A rim rather than a brighter colour, since the colour is what tells a row's name from its value.
	inline FSlateFontInfo outlinedFont(int32 size)
	{
		FSlateFontInfo result = font(size);

		result.OutlineSettings.OutlineSize = 1;
		result.OutlineSettings.OutlineColor = FLinearColor(0.06f, 0.06f, 0.07f, 0.9f);

		return result;
	}

	inline FSlateBrush roundedBrush(const FLinearColor& fill)
	{
		FSlateBrush brush;
		brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		brush.TintColor = FSlateColor(fill);
		brush.OutlineSettings.CornerRadii = FVector4(cornerRadius, cornerRadius, cornerRadius, cornerRadius);
		brush.OutlineSettings.Color = FSlateColor(outline);
		brush.OutlineSettings.Width = 2.0f;
		brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

		return brush;
	}

	// Square edges, for anything that stacks into a list or fills a panel.
	inline FSlateBrush flatBrush(const FLinearColor& fill)
	{
		FSlateBrush brush;
		brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		brush.TintColor = FSlateColor(fill);
		brush.OutlineSettings.CornerRadii = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		brush.OutlineSettings.Width = 0.0f;
		brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

		return brush;
	}

	// One styled text block, which is what every screen built from C++ starts each row with.
	inline UTextBlock* makeLabel(UWidgetTree& tree, const FText& text, int32 size, const FLinearColor& colour)
	{
		UTextBlock* label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		label->SetText(text);
		label->SetFont(font(size));
		label->SetColorAndOpacity(FSlateColor(colour));

		return label;
	}

	inline FButtonStyle buttonStyle()
	{
		FButtonStyle style;
		style.SetNormal(roundedBrush(fillNormal));
		style.SetHovered(roundedBrush(fillHovered));
		style.SetPressed(roundedBrush(fillPressed));
		style.SetNormalPadding(FMargin(12.0f, 6.0f));
		style.SetPressedPadding(FMargin(12.0f, 6.0f));

		return style;
	}

	// One state of a front end button, since fill, outline and radius all change together there.
	inline FSlateBrush stateBrush(const FLinearColor& fill, const FLinearColor& outlineColor, float radius)
	{
		FSlateBrush brush;
		brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		brush.TintColor = FSlateColor(fill);
		brush.OutlineSettings.CornerRadii = FVector4(radius, radius, radius, radius);
		brush.OutlineSettings.Color = FSlateColor(outlineColor);
		brush.OutlineSettings.Width = 2.0f;
		brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

		return brush;
	}

	inline constexpr float SCROLLBAR_THICKNESS = 14.0f;

	inline FSlateBrush scrollbarBrush(const FLinearColor& fill, const FLinearColor& edge)
	{
		FSlateBrush brush = stateBrush(fill, edge, 4.0f);
		brush.ImageSize = FVector2f(SCROLLBAR_THICKNESS, SCROLLBAR_THICKNESS);
		brush.OutlineSettings.Width = 1.0f;
		return brush;
	}

	inline FScrollBarStyle scrollbarStyle()
	{
		const FSlateBrush track = scrollbarBrush(FLinearColor(0.025f, 0.015f, 0.010f, 1.0f), outline);
		const FSlateNoResource hidden;

		// The invisible slots must also reserve zero space, or they make the track wider than its thumb.
		FScrollBarStyle style;
		style.SetHorizontalBackgroundImage(track);
		style.SetVerticalBackgroundImage(track);
		style.SetVerticalTopSlotImage(hidden);
		style.SetHorizontalTopSlotImage(hidden);
		style.SetVerticalBottomSlotImage(hidden);
		style.SetHorizontalBottomSlotImage(hidden);
		style.SetNormalThumbImage(scrollbarBrush(FLinearColor(0.29f, 0.16f, 0.075f, 1.0f), outlineHovered));
		style.SetHoveredThumbImage(scrollbarBrush(outline, outlineHovered));
		style.SetDraggedThumbImage(scrollbarBrush(outlineHovered, labelColor));
		style.SetThickness(SCROLLBAR_THICKNESS);
		return style;
	}

	inline UScrollBox* makeScrollBox(UWidgetTree& tree)
	{
		UScrollBox* scroll = tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		scroll->SetWidgetBarStyle(scrollbarStyle());
		scroll->SetScrollbarThickness(FVector2D(SCROLLBAR_THICKNESS, SCROLLBAR_THICKNESS));
		return scroll;
	}

	// The screens standing in front of the 3D scene share these.
	// A scrim over that scene is the UI fighting the thing it was built to show.
	// One faint wash instead.
	// A fixed width column, centred, because left anchoring slid the screen sideways on a resize.
	// A title owning the top of the screen rather than captioning whatever sits under it.
	inline constexpr FLinearColor frontEndWashColor = FLinearColor(0.01f, 0.01f, 0.015f, 0.2f);
	inline constexpr float frontEndColumnWidth = 520.0f;
	inline constexpr FLinearColor frontEndTitleColor = FLinearColor(0.95f, 0.93f, 0.86f, 1.0f);
	// Smaller than the login screen's ninety two, which that screen keeps for itself.
	// The game's name earns that size once.
	// A line telling you what to do here does not.
	// At ninety two it ran nearly the full width and shouted over the character below it.
	inline constexpr int32 frontEndTitleFontSize = 52;
	inline constexpr int32 frontEndTitleLetterSpacing = 130;
	inline constexpr float frontEndTitleTopPad = 90.0f;
	inline constexpr float frontEndButtonRadius = 8.0f;

	// How far the controls sit off the bottom edge.
	// The screens using it keep their middle clear for whoever stands in the scene behind them.
	inline constexpr float frontEndControlsBottomPad = 90.0f;
	inline constexpr int32 frontEndButtonFontSize = 18;
	inline constexpr int32 frontEndButtonLetterSpacing = 30;

	// Light ink over a dark translucent fill, in the same family as the buttons.
	// The outline is grey rather than their brown, because a field is not a thing to press.
	// Near black rather than light, so it reads as an edge the field sits inside.
	inline constexpr FLinearColor frontEndInk = FLinearColor(0.92f, 0.9f, 0.85f, 1.0f);
	inline constexpr FLinearColor frontEndReadOnlyInk = FLinearColor(0.55f, 0.54f, 0.5f, 1.0f);
	inline constexpr FLinearColor frontEndFieldFill = FLinearColor(0.02f, 0.03f, 0.04f, 0.62f);
	inline constexpr FLinearColor frontEndFieldFillFocused = FLinearColor(0.04f, 0.05f, 0.07f, 0.78f);
	inline constexpr FLinearColor frontEndFieldOutline = FLinearColor(0.05f, 0.05f, 0.06f, 1.0f);
	inline constexpr FLinearColor frontEndFieldOutlineHovered = FLinearColor(0.09f, 0.09f, 0.1f, 1.0f);
	inline constexpr FLinearColor frontEndFieldOutlineFocused = FLinearColor(0.16f, 0.16f, 0.18f, 1.0f);

	// The widget's own style with only the inks and backgrounds replaced.
	// Whatever else the engine put there survives.
	// Drawn as rounded boxes rather than tinted, because the stock brush is an image.
	// An image has no outline to give.
	inline FEditableTextBoxStyle frontEndTextBoxStyle(const FEditableTextBoxStyle& base)
	{
		FEditableTextBoxStyle style = base;
		style.SetForegroundColor(FSlateColor(frontEndInk));
		style.SetReadOnlyForegroundColor(FSlateColor(frontEndReadOnlyInk));
		style.SetFocusedForegroundColor(FSlateColor(frontEndInk));
		style.BackgroundImageNormal = stateBrush(frontEndFieldFill, frontEndFieldOutline, frontEndButtonRadius);
		style.BackgroundImageHovered = stateBrush(frontEndFieldFill, frontEndFieldOutlineHovered, frontEndButtonRadius);
		style.BackgroundImageFocused = stateBrush(frontEndFieldFillFocused, frontEndFieldOutlineFocused, frontEndButtonRadius);
		style.BackgroundImageReadOnly = stateBrush(frontEndFieldFill, frontEndFieldOutline, frontEndButtonRadius);

		return style;
	}

	// The buttons of the converted screens: options at the shared radius, game menu and login at 14.
	// buttonStyle above keeps one outline for the dialogs and rows that were born in C++.
	inline FButtonStyle frontEndButtonStyle(float radius)
	{
		FButtonStyle style;
		style.SetNormal(stateBrush(fillNormal, outline, radius));
		style.SetHovered(stateBrush(fillHovered, outlineHovered, radius));
		style.SetPressed(stateBrush(fillPressed, outlinePressed, radius));
		style.SetNormalPadding(FMargin(12.0f, 6.0f));
		style.SetPressedPadding(FMargin(12.0f, 6.0f));

		return style;
	}

	// One of a set, lit while it is the chosen one.
	// Selected borrows the hovered fill and outline rather than inventing a third pair,
	// since a lit button and a button under the cursor say the same thing to whoever is looking.
	// Hovered matches normal there, so moving the cursor over the chosen one changes nothing.
	inline FButtonStyle frontEndToggleStyle(bool selected)
	{
		if (!selected)
		{
			return frontEndButtonStyle(frontEndButtonRadius);
		}

		FButtonStyle style;
		style.SetNormal(stateBrush(fillHovered, outlineHovered, frontEndButtonRadius));
		style.SetHovered(stateBrush(fillHovered, outlineHovered, frontEndButtonRadius));
		style.SetPressed(stateBrush(fillPressed, outlinePressed, frontEndButtonRadius));
		style.SetNormalPadding(FMargin(12.0f, 6.0f));
		style.SetPressedPadding(FMargin(12.0f, 6.0f));

		return style;
	}

	// A front end button, whole.
	// Two screens each grew their own copy of this and a unity build put both in one translation unit,
	// which is the second time these numbers have been caught living in more than one file.
	inline UButton* frontEndButton(UWidgetTree& tree, const FText& text)
	{
		UButton* button = tree.ConstructWidget<UButton>(UButton::StaticClass());
		button->SetStyle(frontEndButtonStyle(frontEndButtonRadius));

		UTextBlock* label = makeLabel(tree, text, frontEndButtonFontSize, labelColor);
		FSlateFontInfo buttonFont = font(frontEndButtonFontSize);
		buttonFont.LetterSpacing = frontEndButtonLetterSpacing;
		label->SetFont(buttonFont);
		label->SetJustification(ETextJustify::Center);
		button->AddChild(label);

		return button;
	}
}
