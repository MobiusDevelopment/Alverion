#include "UI/OptionsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Sound/SoundBase.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Core/GameSettings.h"
#include "Core/InputBootstrapSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "UI/LocalizedText.h"
#include "UI/UIStyle.h"
#include "UI/UISubsystem.h"
#include "UserSettings/EnhancedInputUserSettings.h"

namespace
{
	// Frame caps offered alongside the monitor's own refresh rate. Zero means uncapped.
	const TArray<int32> frameCapChoices = { 30, 60, 90, 120, 144, 240, 0 };

	FString formatResolution(const FIntPoint& resolution)
	{
		return FString::Printf(TEXT("%d x %d"), resolution.X, resolution.Y);
	}

	FString formatFrameCap(int32 cap)
	{
		return (cap <= 0) ? FString(TEXT("Unlimited")) : FString::Printf(TEXT("%d"), cap);
	}

	// The key rows are built in C++, so they carry the screen's look here rather than in the blueprint.
	// The palette itself lives in UIStyle, so this screen cannot drift from the dialogs that cover it.
	constexpr FLinearColor labelColor = UIStyle::labelColor;
	constexpr FLinearColor unboundColor = UIStyle::mutedColor;

	FText unboundText()
	{
		return LocalizedText::get(TEXT("keybind.unbound"));
	}

	constexpr FLinearColor keyFillNormal = UIStyle::fillNormal;
	constexpr FLinearColor keyFillHovered = UIStyle::fillHovered;
	constexpr FLinearColor keyFillPressed = UIStyle::fillPressed;
	constexpr FLinearColor keyOutline = UIStyle::outline;

	// Matches the labels the blueprint sets on every other row, so a key row does not read as a different screen.
	constexpr int32 rowFontSize = UIStyle::bodyFontSize;

	// Key names run long once a gamepad is involved, so they get their own smaller size.
	const int32 keyFontSize = 17;

	// Every row's label occupies exactly this, so the key buttons line up down the column. Wide enough for the
	// longest name at rowFontSize, since a fixed column clips rather than grows.
	const float labelColumnWidth = 280.0f;

	FSlateFontInfo rowFont(int32 size = rowFontSize)
	{
		return UIStyle::font(size);
	}

	FSlateBrush roundedBrush(const FLinearColor& fill)
	{
		return UIStyle::roundedBrush(fill);
	}

	// Rows want square edges, since a stack of rounded ones reads as loose cards rather than a list.
	FSlateBrush flatBrush(const FLinearColor& fill)
	{
		return UIStyle::flatBrush(fill);
	}

	// An open dropdown defaults to near black rows carrying near black text,
	// which is unreadable against this screen. The list borrows the key buttons' brown instead.
	FTableRowStyle comboItemStyle()
	{
		FTableRowStyle style;
		style.SetEvenRowBackgroundBrush(flatBrush(keyFillNormal));
		style.SetOddRowBackgroundBrush(flatBrush(keyFillNormal));
		style.SetEvenRowBackgroundHoveredBrush(flatBrush(keyFillHovered));
		style.SetOddRowBackgroundHoveredBrush(flatBrush(keyFillHovered));
		style.SetActiveBrush(flatBrush(keyFillHovered));
		style.SetActiveHoveredBrush(flatBrush(keyFillHovered));
		style.SetInactiveBrush(flatBrush(keyFillPressed));
		style.SetInactiveHoveredBrush(flatBrush(keyFillHovered));
		style.SetTextColor(FSlateColor(labelColor));
		style.SetSelectedTextColor(FSlateColor(labelColor));

		return style;
	}

	FButtonStyle keyButtonStyle()
	{
		return UIStyle::buttonStyle();
	}

	// The closed combo defaults to a pale grey box, which is the one light rectangle on a dark screen.
	// Built from the widget's existing style rather than a fresh one,
	// because a default constructed FComboBoxStyle carries an empty down arrow brush,
	// which draws as a solid white square.
	FComboBoxStyle comboButtonStyle(const FComboBoxStyle& base)
	{
		FComboBoxStyle style = base;
		style.ComboButtonStyle.ButtonStyle = keyButtonStyle();
		style.ComboButtonStyle.SetMenuBorderBrush(flatBrush(keyOutline));
		style.ComboButtonStyle.SetMenuBorderPadding(FMargin(1.0f));

		return style;
	}

	// Widget name to string table key. Labels are matched by name so the blueprint needs no extra bindings.
	const TMap<FName, FString> optionsTextKeys =
	{
		{ TEXT("headerText"), TEXT("menu.options_title") },
		{ TEXT("displayTabButtonLabel"), TEXT("options.tab_display") },
		{ TEXT("audioTabButtonLabel"), TEXT("options.tab_audio") },
		{ TEXT("inputTabButtonLabel"), TEXT("options.tab_keybindings") },
		{ TEXT("gameplayTabButtonLabel"), TEXT("options.tab_gameplay") },
		{ TEXT("interfaceTabButtonLabel"), TEXT("options.tab_interface") },
		{ TEXT("resolutionLabel"), TEXT("options.resolution") },
		{ TEXT("windowModeLabel"), TEXT("options.window_mode") },
		{ TEXT("frameCapLabel"), TEXT("options.frame_cap") },
		{ TEXT("qualityLabel"), TEXT("options.quality") },
		{ TEXT("upscalerLabel"), TEXT("options.upscaler") },
		{ TEXT("screenPercentageLabel"), TEXT("options.screen_percentage") },
		{ TEXT("naniteLabel"), TEXT("options.nanite") },
		{ TEXT("lumenLabel"), TEXT("options.lumen") },
		{ TEXT("viewDistanceLabel"), TEXT("options.bucket_view_distance") },
		{ TEXT("shadowLabel"), TEXT("options.bucket_shadow") },
		{ TEXT("globalIlluminationLabel"), TEXT("options.bucket_global_illumination") },
		{ TEXT("reflectionLabel"), TEXT("options.bucket_reflection") },
		{ TEXT("antiAliasingLabel"), TEXT("options.bucket_anti_aliasing") },
		{ TEXT("textureLabel"), TEXT("options.bucket_texture") },
		{ TEXT("effectsLabel"), TEXT("options.bucket_effects") },
		{ TEXT("postProcessingLabel"), TEXT("options.bucket_post_processing") },
		{ TEXT("foliageLabel"), TEXT("options.bucket_foliage") },
		{ TEXT("shadingLabel"), TEXT("options.bucket_shading") },
		{ TEXT("landscapeLabel"), TEXT("options.bucket_landscape") },
		{ TEXT("vsyncLabel"), TEXT("options.vsync") },
		{ TEXT("fovLabel"), TEXT("options.fov") },
		{ TEXT("showFpsLabel"), TEXT("options.show_fps") },
		{ TEXT("showStatsLabel"), TEXT("options.show_stats") },
		{ TEXT("masterVolumeLabel"), TEXT("options.master_volume") },
		{ TEXT("musicVolumeLabel"), TEXT("options.music_volume") },
		{ TEXT("sfxVolumeLabel"), TEXT("options.sfx_volume") },
		{ TEXT("mouseSensitivityLabel"), TEXT("options.sensitivity") },
		{ TEXT("cameraModeLabel"), TEXT("options.camera_mode") },
		{ TEXT("nameplatesLabel"), TEXT("options.nameplates") },
		{ TEXT("damageNumbersLabel"), TEXT("options.damage_numbers") },
		{ TEXT("chatFilterLabel"), TEXT("options.chat_filter") },
		{ TEXT("hudScaleLabel"), TEXT("options.hud_scale") },
		{ TEXT("languageLabel"), TEXT("options.language") },
		{ TEXT("applyButtonLabel"), TEXT("options.apply") },
		{ TEXT("revertButtonLabel"), TEXT("options.revert") },
		{ TEXT("defaultsButtonLabel"), TEXT("options.defaults") },
		{ TEXT("keepChangesButtonLabel"), TEXT("options.keep") },
		{ TEXT("backButtonLabel"), TEXT("options.back") }
	};

	// Everything below rebuilds what WBP_Options held,
	// read out of the asset with the DumpWidget commandlet before it went away.
	constexpr FLinearColor optionsBackdropColor = FLinearColor(0.01f, 0.01f, 0.015f, 0.93f);
	constexpr float optionsPanelWidth = 1150.0f;
	constexpr int32 headerFontSize = 46;

	// The asset set no size on the tab labels, which lands on the engine default.
	constexpr int32 tabFontSize = 24;

	constexpr int32 countdownFontSize = 20;
	constexpr FLinearColor countdownColor = FLinearColor(1.0f, 0.85f, 0.5f, 1.0f);

	// The settings rows align on a minimum label width where the key rows use a fixed SizeBox,
	// because that is how the asset did it and the two column widths differ.
	constexpr float settingsLabelWidth = 260.0f;

	// A label the localization pass finds by name. Its text arrives with refreshLocalizedText.
	UTextBlock* makeNamedLabel(UWidgetTree& tree, const TCHAR* name, int32 size)
	{
		UTextBlock* label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(name));
		label->SetFont(rowFont(size));
		label->SetColorAndOpacity(FSlateColor(labelColor));

		return label;
	}

	// One button in the tab row or the footer, which differ only in label name and size.
	// Not the key rows' style: the asset gave these an outline that brightens on hover.
	UButton* addBarButton(UWidgetTree& tree, UHorizontalBox& row, const TCHAR* labelName, int32 labelSize)
	{
		UButton* button = tree.ConstructWidget<UButton>(UButton::StaticClass());
		button->SetStyle(UIStyle::frontEndButtonStyle(UIStyle::cornerRadius));
		button->AddChild(makeNamedLabel(tree, labelName, labelSize));

		UHorizontalBoxSlot* slot = row.AddChildToHorizontalBox(button);
		slot->SetPadding(FMargin(6.0f, 0.0f, 6.0f, 0.0f));

		return button;
	}

	// One dropdown, styled here rather than in NativeOnInitialized as before,
	// because with a C++ built tree NativeOnInitialized runs before the combos exist,
	// and InitForegroundColor refuses once the Slate widget does.
	UComboBoxString* makeCombo(UWidgetTree& tree)
	{
		UComboBoxString* combo = tree.ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
		combo->SetItemStyle(comboItemStyle());
		combo->SetWidgetStyle(comboButtonStyle(combo->GetWidgetStyle()));

		// InitForegroundColor is protected and there is no runtime setter,
		// so the deprecated field is the only way in. It does exactly what the initializer does,
		// which is assign the member before RebuildWidget reads it.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		combo->ForegroundColor = FSlateColor(labelColor);
		combo->ScrollBarStyle = UIStyle::scrollbarStyle();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		return combo;
	}

	USlider* makeStyledSlider(UWidgetTree& tree)
	{
		USlider* slider = tree.ConstructWidget<USlider>(USlider::StaticClass());

		FSliderStyle style = slider->GetWidgetStyle();
		style.BarThickness = 10.0f;
		slider->SetWidgetStyle(style);

		slider->SetSliderBarColor(UIStyle::mutedColor);
		slider->SetSliderHandleColor(keyOutline);

		return slider;
	}

	// One page of the category switcher: a styled scroll box around a vertical list.
	UVerticalBox* addScrollPage(UWidgetTree& tree, UWidgetSwitcher& switcher)
	{
		UScrollBox* scroll = UIStyle::makeScrollBox(tree);
		scroll->SetScrollbarPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
		scroll->SetAllowOverscroll(false);

		UVerticalBox* page = tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		scroll->AddChild(page);
		switcher.AddChild(scroll);

		return page;
	}

	// One settings row: a right justified label on a fixed column, then whatever control the caller adds.
	UHorizontalBox* addSettingsRow(UWidgetTree& tree, UVerticalBox& page, const TCHAR* labelName)
	{
		UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UTextBlock* label = makeNamedLabel(tree, labelName, rowFontSize);
		label->SetMinDesiredWidth(settingsLabelWidth);
		label->SetJustification(ETextJustify::Right);

		UHorizontalBoxSlot* labelSlot = row->AddChildToHorizontalBox(label);
		labelSlot->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
		labelSlot->SetVerticalAlignment(VAlign_Center);

		UVerticalBoxSlot* rowSlot = page.AddChildToVerticalBox(row);
		rowSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));

		return row;
	}

	// The control filling the rest of its row. Sliders pass extra padding, everything else passes none.
	void addRowControl(UHorizontalBox& row, UWidget& control, const FMargin& padding = FMargin())
	{
		UHorizontalBoxSlot* slot = row.AddChildToHorizontalBox(&control);
		slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		slot->SetVerticalAlignment(VAlign_Center);
		slot->SetPadding(padding);
	}

	// Last entry of the quality dropdown, shown when the buckets below it disagree.
	constexpr int32 customQualityIndex = 5;

	// The engine allows anything down to zero.
	// Zero means leave the project's own value alone rather than naming a scale.
	// Nothing below half renders a readable world either, so the slider offers the useful half.
	constexpr float minScreenPercentage = 50.0f;
	constexpr float maxScreenPercentage = 100.0f;

	struct FUpscalerChoice
	{
		EUpscalerMethod method;
		const TCHAR* textKey;
	};

	// Offered in rising cost. TSR is the engine default and the only true upscaler, the rest are anti-aliasing alone.
	const FUpscalerChoice upscalerChoices[] =
	{
		{ EUpscalerMethod::None, TEXT("options.upscaler_off") },
		{ EUpscalerMethod::Fxaa, TEXT("options.upscaler_fxaa") },
		{ EUpscalerMethod::Smaa, TEXT("options.upscaler_smaa") },
		{ EUpscalerMethod::Taa, TEXT("options.upscaler_taa") },
		{ EUpscalerMethod::Tsr, TEXT("options.upscaler_tsr") }
	};

	int32 indexOfUpscalerMethod(EUpscalerMethod method)
	{
		for (int32 index = 0; index < UE_ARRAY_COUNT(upscalerChoices); index++)
		{
			if (upscalerChoices[index].method == method)
			{
				return index;
			}
		}

		// Whatever the ini held is not on offer, so the screen shows the default rather than an empty box.
		return UE_ARRAY_COUNT(upscalerChoices) - 1;
	}

	// The five scalability levels, in the order the engine numbers them.
	// One list, because twelve dropdowns offer it and twelve copies would drift.
	void fillQualityLevels(UComboBoxString& combo)
	{
		combo.ClearOptions();
		combo.AddOption(LocalizedText::get(TEXT("options.quality_low")).ToString());
		combo.AddOption(LocalizedText::get(TEXT("options.quality_medium")).ToString());
		combo.AddOption(LocalizedText::get(TEXT("options.quality_high")).ToString());
		combo.AddOption(LocalizedText::get(TEXT("options.quality_epic")).ToString());
		combo.AddOption(LocalizedText::get(TEXT("options.quality_cinematic")).ToString());
	}

	// A bucket dropdown showing what the engine currently holds for it.
	void fillBucket(UComboBoxString* combo, int32 level)
	{
		if (combo == nullptr)
		{
			return;
		}

		fillQualityLevels(*combo);
		combo->SetSelectedIndex(FMath::Clamp(level, 0, 4));
	}

	// Pushes a bucket dropdown's choice back through whichever setter owns it.
	// The setter is passed in because UGameUserSettings names eleven of them rather than taking an index.
	void applyBucket(const UComboBoxString* combo, TFunctionRef<void(int32)> setter)
	{
		if (combo == nullptr)
		{
			return;
		}

		const int32 selected = combo->GetSelectedIndex();
		if (selected < 0)
		{
			return;
		}

		setter(FMath::Clamp(selected, 0, 4));
	}

	// The quality buckets sit two to a row, so their labels get their own narrower column than the rows above.
	constexpr float bucketLabelWidth = 240.0f;
	constexpr float bucketColumnGap = 24.0f;
	constexpr float bucketLabelPadding = 12.0f;

	// An empty row for the buckets to be laid into, two cells at a time.
	UHorizontalBox* addColumnRow(UWidgetTree& tree, UVerticalBox& page)
	{
		UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UVerticalBoxSlot* rowSlot = page.AddChildToVerticalBox(row);
		rowSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));

		return row;
	}

	// One half of a two column row: a right justified label then its control.
	// A label longer than the column eats into its own control rather than shifting the one beside it.
	void addColumnCell(UWidgetTree& tree, UHorizontalBox& row, const TCHAR* labelName, UWidget& control, bool isSecondColumn)
	{
		UTextBlock* label = makeNamedLabel(tree, labelName, rowFontSize);
		label->SetMinDesiredWidth(bucketLabelWidth);
		label->SetJustification(ETextJustify::Right);

		UHorizontalBoxSlot* labelSlot = row.AddChildToHorizontalBox(label);
		labelSlot->SetPadding(FMargin(isSecondColumn ? bucketColumnGap : 0.0f, 0.0f, bucketLabelPadding, 0.0f));
		labelSlot->SetVerticalAlignment(VAlign_Center);

		UHorizontalBoxSlot* controlSlot = row.AddChildToHorizontalBox(&control);
		controlSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		controlSlot->SetVerticalAlignment(VAlign_Center);
	}

	// Eleven is odd, so the last bucket has no partner.
	// An empty half keeps its dropdown the same width as the ten above rather than twice it.
	void addEmptyColumnCell(UWidgetTree& tree, UHorizontalBox& row)
	{
		USizeBox* labelSpace = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
		labelSpace->SetWidthOverride(bucketColumnGap + bucketLabelWidth + bucketLabelPadding);
		row.AddChildToHorizontalBox(labelSpace);

		USpacer* comboSpace = tree.ConstructWidget<USpacer>(USpacer::StaticClass());
		UHorizontalBoxSlot* comboSlot = row.AddChildToHorizontalBox(comboSpace);
		comboSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// The plain number beside a slider. The asset left it entirely unstyled, so this does too.
	UTextBlock* addRowValueText(UWidgetTree& tree, UHorizontalBox& row, const TCHAR* name)
	{
		UTextBlock* text = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(name));

		UHorizontalBoxSlot* slot = row.AddChildToHorizontalBox(text);
		slot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
		slot->SetVerticalAlignment(VAlign_Center);

		return text;
	}
}

TSharedRef<SWidget> UOptionsWidget::RebuildWidget()
{
	if ((WidgetTree != nullptr) && (WidgetTree->RootWidget == nullptr))
	{
		// A dimmed backdrop over the whole screen, with the panel centered on top of it.
		UOverlay* root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());

		UImage* backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush backdropBrush;
		backdropBrush.TintColor = FSlateColor(optionsBackdropColor);
		backdrop->SetBrush(backdropBrush);

		UOverlaySlot* backdropSlot = root->AddChildToOverlay(backdrop);
		backdropSlot->SetHorizontalAlignment(HAlign_Fill);
		backdropSlot->SetVerticalAlignment(VAlign_Fill);

		USizeBox* panel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		panel->SetWidthOverride(optionsPanelWidth);

		UOverlaySlot* panelSlot = root->AddChildToOverlay(panel);
		panelSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 24.0f));
		panelSlot->SetHorizontalAlignment(HAlign_Center);
		panelSlot->SetVerticalAlignment(VAlign_Fill);

		UVerticalBox* body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		panel->AddChild(body);

		UTextBlock* header = makeNamedLabel(*WidgetTree, TEXT("headerText"), headerFontSize);
		UVerticalBoxSlot* headerSlot = body->AddChildToVerticalBox(header);
		headerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		headerSlot->SetHorizontalAlignment(HAlign_Center);

		UHorizontalBox* tabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		displayTabButton = addBarButton(*WidgetTree, *tabRow, TEXT("displayTabButtonLabel"), tabFontSize);
		audioTabButton = addBarButton(*WidgetTree, *tabRow, TEXT("audioTabButtonLabel"), tabFontSize);
		inputTabButton = addBarButton(*WidgetTree, *tabRow, TEXT("inputTabButtonLabel"), tabFontSize);
		gameplayTabButton = addBarButton(*WidgetTree, *tabRow, TEXT("gameplayTabButtonLabel"), tabFontSize);
		interfaceTabButton = addBarButton(*WidgetTree, *tabRow, TEXT("interfaceTabButtonLabel"), tabFontSize);

		UVerticalBoxSlot* tabSlot = body->AddChildToVerticalBox(tabRow);
		tabSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 22.0f));
		tabSlot->SetHorizontalAlignment(HAlign_Center);

		categorySwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
		categorySwitcher->SetClipping(EWidgetClipping::ClipToBounds);

		UVerticalBoxSlot* switcherSlot = body->AddChildToVerticalBox(categorySwitcher);
		switcherSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		switcherSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 10.0f));
		switcherSlot->SetVerticalAlignment(VAlign_Top);

		// Display.
		UVerticalBox* displayPage = addScrollPage(*WidgetTree, *categorySwitcher);

		resolutionCombo = makeCombo(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *displayPage, TEXT("resolutionLabel")), *resolutionCombo);

		windowModeCombo = makeCombo(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *displayPage, TEXT("windowModeLabel")), *windowModeCombo);

		// Frame cap and vsync share a row, being two halves of the same question about frame pacing.
		frameCapCombo = makeCombo(*WidgetTree);
		vsyncCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());

		UHorizontalBox* frameRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *frameRow, TEXT("frameCapLabel"), *frameCapCombo, false);
		addColumnCell(*WidgetTree, *frameRow, TEXT("vsyncLabel"), *vsyncCheck, true);

		qualityCombo = makeCombo(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *displayPage, TEXT("qualityLabel")), *qualityCombo);

		upscalerCombo = makeCombo(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *displayPage, TEXT("upscalerLabel")), *upscalerCombo);

		const FMargin sliderPadding = FMargin(0.0f, 8.0f, 0.0f, 8.0f);

		UHorizontalBox* screenPercentageRow = addSettingsRow(*WidgetTree, *displayPage, TEXT("screenPercentageLabel"));
		screenPercentageSlider = makeStyledSlider(*WidgetTree);
		addRowControl(*screenPercentageRow, *screenPercentageSlider, sliderPadding);
		screenPercentageValueText = addRowValueText(*WidgetTree, *screenPercentageRow, TEXT("screenPercentageValueText"));

		// The renderer toggles pair off the same way the quality buckets below do,
		// because a checkbox needs a row no less than a dropdown and there are five of them on this page.
		naniteCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		lumenCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());

		UHorizontalBox* rendererRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *rendererRow, TEXT("naniteLabel"), *naniteCheck, false);
		addColumnCell(*WidgetTree, *rendererRow, TEXT("lumenLabel"), *lumenCheck, true);

		UHorizontalBox* fovRow = addSettingsRow(*WidgetTree, *displayPage, TEXT("fovLabel"));
		fovSlider = makeStyledSlider(*WidgetTree);
		addRowControl(*fovRow, *fovSlider, sliderPadding);
		fovValueText = addRowValueText(*WidgetTree, *fovRow, TEXT("fovValueText"));

		// The two overlays pair off as well, being the same question asked twice.
		showFpsCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		showStatsCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());

		UHorizontalBox* overlayRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *overlayRow, TEXT("showFpsLabel"), *showFpsCheck, false);
		addColumnCell(*WidgetTree, *overlayRow, TEXT("showStatsLabel"), *showStatsCheck, true);

		// The per-bucket overrides, below the tier that writes them all.
		// Two to a row, because eleven single rows put this page past a screen on their own.
		viewDistanceCombo = makeCombo(*WidgetTree);
		shadowCombo = makeCombo(*WidgetTree);
		globalIlluminationCombo = makeCombo(*WidgetTree);
		reflectionCombo = makeCombo(*WidgetTree);
		antiAliasingCombo = makeCombo(*WidgetTree);
		textureCombo = makeCombo(*WidgetTree);
		effectsCombo = makeCombo(*WidgetTree);
		postProcessingCombo = makeCombo(*WidgetTree);
		foliageCombo = makeCombo(*WidgetTree);
		shadingCombo = makeCombo(*WidgetTree);
		landscapeCombo = makeCombo(*WidgetTree);

		UHorizontalBox* firstBucketRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *firstBucketRow, TEXT("viewDistanceLabel"), *viewDistanceCombo, false);
		addColumnCell(*WidgetTree, *firstBucketRow, TEXT("shadowLabel"), *shadowCombo, true);

		UHorizontalBox* secondBucketRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *secondBucketRow, TEXT("globalIlluminationLabel"), *globalIlluminationCombo, false);
		addColumnCell(*WidgetTree, *secondBucketRow, TEXT("reflectionLabel"), *reflectionCombo, true);

		UHorizontalBox* thirdBucketRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *thirdBucketRow, TEXT("antiAliasingLabel"), *antiAliasingCombo, false);
		addColumnCell(*WidgetTree, *thirdBucketRow, TEXT("textureLabel"), *textureCombo, true);

		UHorizontalBox* fourthBucketRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *fourthBucketRow, TEXT("effectsLabel"), *effectsCombo, false);
		addColumnCell(*WidgetTree, *fourthBucketRow, TEXT("postProcessingLabel"), *postProcessingCombo, true);

		UHorizontalBox* fifthBucketRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *fifthBucketRow, TEXT("foliageLabel"), *foliageCombo, false);
		addColumnCell(*WidgetTree, *fifthBucketRow, TEXT("shadingLabel"), *shadingCombo, true);

		UHorizontalBox* sixthBucketRow = addColumnRow(*WidgetTree, *displayPage);
		addColumnCell(*WidgetTree, *sixthBucketRow, TEXT("landscapeLabel"), *landscapeCombo, false);
		addEmptyColumnCell(*WidgetTree, *sixthBucketRow);

		// Audio.
		UVerticalBox* audioPage = addScrollPage(*WidgetTree, *categorySwitcher);

		masterVolumeSlider = makeStyledSlider(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *audioPage, TEXT("masterVolumeLabel")), *masterVolumeSlider, sliderPadding);

		musicVolumeSlider = makeStyledSlider(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *audioPage, TEXT("musicVolumeLabel")), *musicVolumeSlider, sliderPadding);

		sfxVolumeSlider = makeStyledSlider(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *audioPage, TEXT("sfxVolumeLabel")), *sfxVolumeSlider, sliderPadding);

		// Input. Just the box the key rows build into, which is all the asset held too.
		UVerticalBox* inputPage = addScrollPage(*WidgetTree, *categorySwitcher);

		keyBindingBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		inputPage->AddChildToVerticalBox(keyBindingBox);

		// Gameplay. Sits between Input and Interface, the order the categories are listed in.
		UVerticalBox* gameplayPage = addScrollPage(*WidgetTree, *categorySwitcher);

		cameraModeCombo = makeCombo(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *gameplayPage, TEXT("cameraModeLabel")), *cameraModeCombo);

		nameplatesCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		addRowControl(*addSettingsRow(*WidgetTree, *gameplayPage, TEXT("nameplatesLabel")), *nameplatesCheck);

		damageNumbersCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		addRowControl(*addSettingsRow(*WidgetTree, *gameplayPage, TEXT("damageNumbersLabel")), *damageNumbersCheck);

		chatFilterCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		addRowControl(*addSettingsRow(*WidgetTree, *gameplayPage, TEXT("chatFilterLabel")), *chatFilterCheck);

		// Interface.
		UVerticalBox* interfacePage = addScrollPage(*WidgetTree, *categorySwitcher);

		UHorizontalBox* hudScaleRow = addSettingsRow(*WidgetTree, *interfacePage, TEXT("hudScaleLabel"));
		hudScaleSlider = makeStyledSlider(*WidgetTree);
		addRowControl(*hudScaleRow, *hudScaleSlider, sliderPadding);
		hudScaleValueText = addRowValueText(*WidgetTree, *hudScaleRow, TEXT("hudScaleValueText"));

		languageCombo = makeCombo(*WidgetTree);
		addRowControl(*addSettingsRow(*WidgetTree, *interfacePage, TEXT("languageLabel")), *languageCombo);

		// Footer.
		UHorizontalBox* footerRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		applyButton = addBarButton(*WidgetTree, *footerRow, TEXT("applyButtonLabel"), rowFontSize);
		revertButton = addBarButton(*WidgetTree, *footerRow, TEXT("revertButtonLabel"), rowFontSize);
		defaultsButton = addBarButton(*WidgetTree, *footerRow, TEXT("defaultsButtonLabel"), rowFontSize);
		keepChangesButton = addBarButton(*WidgetTree, *footerRow, TEXT("keepChangesButtonLabel"), rowFontSize);
		backButton = addBarButton(*WidgetTree, *footerRow, TEXT("backButtonLabel"), rowFontSize);

		UVerticalBoxSlot* footerSlot = body->AddChildToVerticalBox(footerRow);
		footerSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));
		footerSlot->SetHorizontalAlignment(HAlign_Center);
		footerSlot->SetVerticalAlignment(VAlign_Bottom);

		revertCountdownText = makeNamedLabel(*WidgetTree, TEXT("revertCountdownText"), countdownFontSize);
		revertCountdownText->SetColorAndOpacity(FSlateColor(countdownColor));

		UVerticalBoxSlot* countdownSlot = body->AddChildToVerticalBox(revertCountdownText);
		countdownSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 8.0f));
		countdownSlot->SetHorizontalAlignment(HAlign_Center);

		WidgetTree->RootWidget = root;

		// The class default only lived in the asset, so it is loaded here rather than lost with the blueprint.
		if (clickSound == nullptr)
		{
			clickSound = LoadObject<USoundBase>(nullptr, UIStyle::clickSoundPath);
		}
	}

	return Super::RebuildWidget();
}

void UOptionsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (displayTabButton != nullptr)
	{
		displayTabButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleDisplayTabClicked);
	}

	if (audioTabButton != nullptr)
	{
		audioTabButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleAudioTabClicked);
	}

	if (inputTabButton != nullptr)
	{
		inputTabButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleInputTabClicked);
	}

	if (gameplayTabButton != nullptr)
	{
		gameplayTabButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleGameplayTabClicked);
	}

	if (interfaceTabButton != nullptr)
	{
		interfaceTabButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleInterfaceTabClicked);
	}

	if (applyButton != nullptr)
	{
		applyButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleApplyClicked);
	}

	if (revertButton != nullptr)
	{
		revertButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleRevertClicked);
	}

	if (defaultsButton != nullptr)
	{
		defaultsButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleDefaultsClicked);
	}

	if (backButton != nullptr)
	{
		backButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleBackClicked);
	}

	if (keepChangesButton != nullptr)
	{
		keepChangesButton->OnClicked.AddUniqueDynamic(this, &UOptionsWidget::handleKeepChangesClicked);
	}

	if (fovSlider != nullptr)
	{
		fovSlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleFovChanged);
	}

	if (mouseSensitivitySlider != nullptr)
	{
		mouseSensitivitySlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleMouseSensitivityChanged);
	}

	if (screenPercentageSlider != nullptr)
	{
		screenPercentageSlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleScreenPercentageChanged);
	}

	if (hudScaleSlider != nullptr)
	{
		hudScaleSlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleHudScaleChanged);
	}

	if (masterVolumeSlider != nullptr)
	{
		masterVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleMasterVolumeChanged);
	}

	if (musicVolumeSlider != nullptr)
	{
		musicVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleMusicVolumeChanged);
	}

	if (sfxVolumeSlider != nullptr)
	{
		sfxVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UOptionsWidget::handleSfxVolumeChanged);
	}

	// Applying a language switches the running culture under a screen that is still open,
	// so the labels rebuild from the same delegate the engine uses rather than waiting for a reopen.
	_cultureChangedHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UOptionsWidget::refreshLocalizedText);

	refreshLocalizedText();

	// The countdown widgets only belong on screen while a resolution change is pending.
	finishResolutionCountdown(true);

	SetIsFocusable(true);
}

void UOptionsWidget::NativeDestruct()
{
	// The internationalization singleton outlives this widget, so a delegate left behind would fire into freed memory.
	if (_cultureChangedHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(_cultureChangedHandle);
		_cultureChangedHandle.Reset();
	}

	Super::NativeDestruct();
}

void UOptionsWidget::refreshLocalizedText()
{
	LocalizedText::applyToTree(WidgetTree, optionsTextKeys);

	// The combo entries and the key rows carry translated text of their own,
	// and neither is part of the widget tree the pass above walks, so both are rebuilt.
	// Each populate reads its value back out of the settings, which is what keeps the current selection.
	populateDisplay();
	populateAudio();
	populateInput();
	populateGameplay();
	populateInterface();
}

void UOptionsWidget::NativeTick(const FGeometry& geometry, float deltaSeconds)
{
	Super::NativeTick(geometry, deltaSeconds);

	if (_revertSecondsLeft <= 0.0f)
	{
		return;
	}

	_revertSecondsLeft -= deltaSeconds;

	if (revertCountdownText != nullptr)
	{
		const int32 secondsLeft = FMath::Max(0, FMath::CeilToInt(_revertSecondsLeft));
		revertCountdownText->SetText(FText::FromString(FString::Printf(TEXT("Keep these display settings? Reverting in %d..."), secondsLeft)));
	}

	if (_revertSecondsLeft <= 0.0f)
	{
		finishResolutionCountdown(false);
	}
}

FReply UOptionsWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
	if (applyPendingRebind(keyEvent.GetKey()))
	{
		return FReply::Handled();
	}

	// Escape leaves the way Back does, in the front end as well as over the world. A rebind in progress took it above.
	if (keyEvent.GetKey() == EKeys::Escape)
	{
		UGameInstance* gameInstance = GetGameInstance();
		UUISubsystem* ui = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
		if ((ui == nullptr) || ui->acceptEscape(keyEvent.IsRepeat()))
		{
			handleBackClicked();
		}

		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(geometry, keyEvent);
}

FReply UOptionsWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& pointerEvent)
{
	if (applyPendingRebind(pointerEvent.GetEffectingButton()))
	{
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(geometry, pointerEvent);
}

UWidget* UOptionsWidget::NativeGetDesiredFocusTarget() const
{
	if (displayTabButton != nullptr)
	{
		return displayTabButton;
	}

	return Super::NativeGetDesiredFocusTarget();
}

void UOptionsWidget::populateDisplay()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	if (resolutionCombo != nullptr)
	{
		resolutionCombo->ClearOptions();

		const TArray<FIntPoint> resolutions = UGameSettings::getSupportedResolutions();
		for (const FIntPoint& resolution : resolutions)
		{
			resolutionCombo->AddOption(formatResolution(resolution));
		}

		resolutionCombo->SetSelectedOption(formatResolution(settings->GetScreenResolution()));
	}

	if (windowModeCombo != nullptr)
	{
		windowModeCombo->ClearOptions();
		windowModeCombo->AddOption(TEXT("Windowed"));
		windowModeCombo->AddOption(TEXT("Borderless Fullscreen"));
		windowModeCombo->SetSelectedIndex((settings->getClientWindowMode() == EClientWindowMode::Windowed) ? 0 : 1);
	}

	if (frameCapCombo != nullptr)
	{
		frameCapCombo->ClearOptions();

		const int32 currentCap = FMath::RoundToInt(settings->GetFrameRateLimit());
		const int32 nativeRate = UGameSettings::getNativeRefreshRate();
		const FString monitorOption = FString::Printf(TEXT("%d (monitor)"), nativeRate);

		frameCapCombo->AddOption(monitorOption);
		for (int32 cap : frameCapChoices)
		{
			frameCapCombo->AddOption(formatFrameCap(cap));
		}

		frameCapCombo->SetSelectedOption((currentCap == nativeRate) ? monitorOption : formatFrameCap(currentCap));
	}

	if (qualityCombo != nullptr)
	{
		fillQualityLevels(*qualityCombo);

		// Custom is last and is what the engine means by -1: the buckets below do not all agree.
		// Without it a custom setup reads as Low, which is what clamping a -1 into the list did.
		qualityCombo->AddOption(LocalizedText::get(TEXT("options.quality_custom")).ToString());

		const int32 overallLevel = settings->GetOverallScalabilityLevel();
		qualityCombo->SetSelectedIndex((overallLevel < 0) ? customQualityIndex : FMath::Clamp(overallLevel, 0, 4));
	}

	if (upscalerCombo != nullptr)
	{
		upscalerCombo->ClearOptions();
		for (const FUpscalerChoice& choice : upscalerChoices)
		{
			upscalerCombo->AddOption(LocalizedText::get(choice.textKey).ToString());
		}

		upscalerCombo->SetSelectedIndex(indexOfUpscalerMethod(settings->getUpscalerMethod()));
	}

	if (screenPercentageSlider != nullptr)
	{
		// The engine owns r.ScreenPercentage through its own resolution quality,
		// so this drives that rather than writing the cvar behind its back.
		float currentNormalized = 0.0f;
		float currentValue = 0.0f;
		float engineMinimum = 0.0f;
		float engineMaximum = 0.0f;
		settings->GetResolutionScaleInformationEx(currentNormalized, currentValue, engineMinimum, engineMaximum);

		screenPercentageSlider->SetMinValue(minScreenPercentage);
		screenPercentageSlider->SetMaxValue(maxScreenPercentage);

		// Zero is not a scale. It is the engine saying nobody overrode the project's own screen percentage,
		// which DefaultEngine.ini sets to 100, so a fresh install would otherwise show a slider reading 0%.
		const float shownValue = (currentValue <= 0.0f) ? maxScreenPercentage : FMath::Clamp(currentValue, minScreenPercentage, maxScreenPercentage);
		screenPercentageSlider->SetValue(shownValue);
		handleScreenPercentageChanged(shownValue);
	}

	if (naniteCheck != nullptr)
	{
		naniteCheck->SetIsChecked(settings->getNaniteEnabled());
	}

	if (lumenCheck != nullptr)
	{
		lumenCheck->SetIsChecked(settings->getLumenEnabled());
	}

	fillBucket(viewDistanceCombo, settings->GetViewDistanceQuality());
	fillBucket(shadowCombo, settings->GetShadowQuality());
	fillBucket(globalIlluminationCombo, settings->GetGlobalIlluminationQuality());
	fillBucket(reflectionCombo, settings->GetReflectionQuality());
	fillBucket(antiAliasingCombo, settings->GetAntiAliasingQuality());
	fillBucket(textureCombo, settings->GetTextureQuality());
	fillBucket(effectsCombo, settings->GetVisualEffectQuality());
	fillBucket(postProcessingCombo, settings->GetPostProcessingQuality());
	fillBucket(foliageCombo, settings->GetFoliageQuality());
	fillBucket(shadingCombo, settings->GetShadingQuality());
	fillBucket(landscapeCombo, settings->GetLandscapeQuality());

	if (vsyncCheck != nullptr)
	{
		vsyncCheck->SetIsChecked(settings->IsVSyncEnabled());
	}

	if (showFpsCheck != nullptr)
	{
		showFpsCheck->SetIsChecked(settings->getShowFps());
	}

	if (showStatsCheck != nullptr)
	{
		showStatsCheck->SetIsChecked(settings->getShowStats());
	}

	if (fovSlider != nullptr)
	{
		fovSlider->SetMinValue(UGameSettings::minFieldOfView);
		fovSlider->SetMaxValue(UGameSettings::maxFieldOfView);
		fovSlider->SetValue(settings->getFieldOfView());
		handleFovChanged(settings->getFieldOfView());
	}
}

void UOptionsWidget::populateAudio()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	if (masterVolumeSlider != nullptr)
	{
		masterVolumeSlider->SetValue(settings->getMasterVolume());
	}

	if (musicVolumeSlider != nullptr)
	{
		musicVolumeSlider->SetValue(settings->getMusicVolume());
	}

	if (sfxVolumeSlider != nullptr)
	{
		sfxVolumeSlider->SetValue(settings->getSfxVolume());
	}
}

void UOptionsWidget::populateInput()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	if (mouseSensitivitySlider != nullptr)
	{
		mouseSensitivitySlider->SetMinValue(UGameSettings::minMouseSensitivity);
		mouseSensitivitySlider->SetMaxValue(2.0f);
		mouseSensitivitySlider->SetValue(settings->getMouseSensitivity());
		handleMouseSensitivityChanged(settings->getMouseSensitivity());
	}

	rebuildKeyBindingRows();
}

void UOptionsWidget::populateGameplay()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	if (cameraModeCombo != nullptr)
	{
		cameraModeCombo->ClearOptions();
		cameraModeCombo->AddOption(LocalizedText::get(TEXT("options.camera_third_person")).ToString());
		cameraModeCombo->AddOption(LocalizedText::get(TEXT("options.camera_first_person")).ToString());
		cameraModeCombo->SetSelectedIndex((settings->getCameraMode() == ECameraMode::ThirdPerson) ? 0 : 1);
	}

	if (nameplatesCheck != nullptr)
	{
		nameplatesCheck->SetIsChecked(settings->getShowNameplates());
	}

	if (damageNumbersCheck != nullptr)
	{
		damageNumbersCheck->SetIsChecked(settings->getShowDamageNumbers());
	}

	if (chatFilterCheck != nullptr)
	{
		chatFilterCheck->SetIsChecked(settings->getFilterChatProfanity());
	}
}

void UOptionsWidget::populateInterface()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	if (hudScaleSlider != nullptr)
	{
		hudScaleSlider->SetMinValue(UGameSettings::minHudScale);
		hudScaleSlider->SetMaxValue(UGameSettings::maxHudScale);
		hudScaleSlider->SetValue(settings->getHudScale());
		handleHudScaleChanged(settings->getHudScale());
	}

	if (languageCombo != nullptr)
	{
		languageCombo->ClearOptions();

		// Listed by native name, selected by index, because the option text is not the culture code.
		const TArray<FString> cultures = UGameSettings::getAvailableCultures();
		for (const FString& culture : cultures)
		{
			languageCombo->AddOption(UGameSettings::getCultureDisplayName(culture));
		}

		const int32 currentIndex = cultures.IndexOfByKey(settings->getCultureCode());
		languageCombo->SetSelectedIndex((currentIndex != INDEX_NONE) ? currentIndex : 0);
	}
}

void UOptionsWidget::rebuildKeyBindingRows()
{
	if (keyBindingBox == nullptr || WidgetTree == nullptr)
	{
		return;
	}

	keyBindingBox->ClearChildren();

	const ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	if (localPlayer == nullptr)
	{
		return;
	}

	const UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (inputSubsystem == nullptr)
	{
		return;
	}

	UEnhancedInputUserSettings* inputSettings = inputSubsystem->GetUserSettings();
	if (inputSettings == nullptr)
	{
		return;
	}

	// The bootstrap subsystem registers during local player init, before Enhanced Input has user settings to
	// register into, so it always asks again here. A saved profile can also hold stale rows from an older action
	// set, and one of those is enough to make a conditional retry skip a list that is otherwise wrong.
	UInputBootstrapSubsystem* bootstrap = localPlayer->GetSubsystem<UInputBootstrapSubsystem>();
	if (bootstrap != nullptr)
	{
		bootstrap->registerWithUserSettings();
	}

	UEnhancedPlayerMappableKeyProfile* profile = inputSettings->GetActiveKeyProfile();
	if (profile == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("OptionsWidget found no active key profile, so no keys can be listed."));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("OptionsWidget is listing %d key mapping rows."), profile->GetPlayerMappingRows().Num());

	_rebindHandlers.Reset();

	// One row per action with a button per slot, so a keyboard key and a gamepad button can both drive it.
	const EPlayerMappableKeySlot editableSlots[] = { EPlayerMappableKeySlot::First, EPlayerMappableKeySlot::Second };

	// The order is the binding table's, not the map's.
	// GetPlayerMappingRows is a TMap, so iterating it straight puts every row wherever its name hashes, which is stable within a run and meaningless between them.
	TArray<FName> orderedNames = (bootstrap != nullptr) ? bootstrap->getDefaultMappingOrder() : TArray<FName>();

	// Anything the profile holds that the table does not name, such as a row left over from an older action set, still gets listed rather than vanishing.
	for (const TPair<FName, FKeyMappingRow>& row : profile->GetPlayerMappingRows())
	{
		orderedNames.AddUnique(row.Key);
	}

	for (const FName& mappingName : orderedNames)
	{
		const FKeyMappingRow* mappingRow = profile->GetPlayerMappingRows().Find(mappingName);
		if ((mappingRow == nullptr) || mappingRow->Mappings.IsEmpty())
		{
			continue;
		}

		const FPlayerKeyMapping& firstMapping = *mappingRow->Mappings.CreateConstIterator();

		UHorizontalBox* rowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		// The action's own display name is a literal, so the translated one is looked up by mapping name and
		// the literal only stands in when the table has no entry.
		const FString mappingKey = FString::Printf(TEXT("keybind.%s"), *mappingName.ToString());

		// Asked for with a fallback, because a key the table does not hold comes back as the engine complaining in angle brackets rather than as empty text, and that complaint was being drawn as the label.
		const FText translatedName = LocalizedText::getOr(mappingKey, firstMapping.GetDisplayName());

		UTextBlock* actionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		actionText->SetText(translatedName);
		actionText->SetFont(rowFont());
		actionText->SetColorAndOpacity(FSlateColor(labelColor));
		actionText->SetJustification(ETextJustify::Right);

		// A fixed column rather than an automatic one, otherwise a long name widens its own row and the buttons
		// on that row start further right than everyone else's.
		USizeBox* labelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		labelBox->SetWidthOverride(labelColumnWidth);
		labelBox->AddChild(actionText);

		UHorizontalBoxSlot* actionSlot = rowBox->AddChildToHorizontalBox(labelBox);
		if (actionSlot != nullptr)
		{
			actionSlot->SetVerticalAlignment(VAlign_Center);
			actionSlot->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
			actionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		for (EPlayerMappableKeySlot slot : editableSlots)
		{
			const FPlayerKeyMapping* slotMapping = nullptr;
			for (const FPlayerKeyMapping& candidate : mappingRow->Mappings)
			{
				if (candidate.GetSlot() == slot)
				{
					slotMapping = &candidate;
					break;
				}
			}

			UButton* keyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			keyButton->SetStyle(keyButtonStyle());

			// A slot cleared by a conflict still has a mapping, it just holds an invalid key,
			// so the key itself decides whether the row reads as bound rather than the mapping existing.
			const bool isBound = (slotMapping != nullptr) && slotMapping->GetCurrentKey().IsValid();

			UTextBlock* keyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			keyText->SetText(isBound ? slotMapping->GetCurrentKey().GetDisplayName() : unboundText());
			keyText->SetFont(rowFont(keyFontSize));
			keyText->SetColorAndOpacity(FSlateColor(isBound ? FLinearColor::White : unboundColor));
			keyText->SetJustification(ETextJustify::Center);

			// A name too long for its button is cut rather than allowed to run over the neighbour.
			keyText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			keyText->SetAutoWrapText(false);
			keyButton->AddChild(keyText);

			UHorizontalBoxSlot* keySlot = rowBox->AddChildToHorizontalBox(keyButton);
			if (keySlot != nullptr)
			{
				keySlot->SetVerticalAlignment(VAlign_Center);
				keySlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
				keySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			// The dynamic delegate cannot carry the row, so a handler object remembers it instead.
			UKeyRebindHandler* handler = NewObject<UKeyRebindHandler>(this);
			handler->mappingName = mappingName;
			handler->slot = slot;
			handler->owner = this;
			handler->keyText = keyText;
			_rebindHandlers.Add(handler);

			keyButton->OnClicked.AddDynamic(handler, &UKeyRebindHandler::handleClicked);
		}

		UVerticalBoxSlot* rowSlot = keyBindingBox->AddChildToVerticalBox(rowBox);
		if (rowSlot != nullptr)
		{
			// Same rhythm as the rows the blueprint lays out on the other tabs.
			rowSlot->SetHorizontalAlignment(HAlign_Fill);
			rowSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));
		}
	}
}

void UOptionsWidget::restoreDefaultKeyBindings()
{
	ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	if (localPlayer == nullptr)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (inputSubsystem == nullptr)
	{
		return;
	}

	UEnhancedInputUserSettings* inputSettings = inputSubsystem->GetUserSettings();
	if (inputSettings == nullptr)
	{
		return;
	}

	// Throws away every saved rebind, which is also how rows left over from an older action set are flushed.
	FGameplayTagContainer failureReason;
	inputSettings->ResetKeyProfileIdToDefault(inputSettings->GetActiveKeyProfileId(), failureReason);
	inputSettings->SaveSettings();

	if (!failureReason.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("OptionsWidget could not reset the key profile: %s"), *failureReason.ToString());
	}
}

void UOptionsWidget::beginListening(FName mappingName, EPlayerMappableKeySlot slot, UTextBlock* keyText)
{
	_listeningMappingName = mappingName;
	_listeningSlot = slot;
	_listeningKeyText = keyText;

	if (keyText != nullptr)
	{
		keyText->SetText(FText::FromString(TEXT("Press a key...")));
	}

	SetKeyboardFocus();
}

void UKeyRebindHandler::handleClicked()
{
	if (owner != nullptr)
	{
		owner->beginListening(mappingName, slot, keyText);
	}
}

bool UOptionsWidget::applyPendingRebind(const FKey& key)
{
	if (_listeningMappingName.IsNone())
	{
		return false;
	}

	const FName mappingName = _listeningMappingName;
	_listeningMappingName = NAME_None;

	// Escape cancels, which is the only way out when a key turns out to be unusable.
	if (key == EKeys::Escape)
	{
		rebuildKeyBindingRows();
		return true;
	}

	ULocalPlayer* localPlayer = GetOwningLocalPlayer();
	if (localPlayer == nullptr)
	{
		return true;
	}

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (inputSubsystem == nullptr)
	{
		return true;
	}

	UEnhancedInputUserSettings* inputSettings = inputSubsystem->GetUserSettings();
	if (inputSettings == nullptr)
	{
		return true;
	}

	// Enhanced Input accepts the same key on two actions without complaint,
	// so a player could bind W twice and only find out in game. The new binding wins and the old one is cleared,
	// which is what rebinding does everywhere else, and the row that lost its key returns empty in the rebuild.
	clearConflictingBindings(inputSettings, key, mappingName, _listeningSlot);

	FMapPlayerKeyArgs args;
	args.MappingName = mappingName;
	args.Slot = _listeningSlot;
	args.NewKey = key;

	FGameplayTagContainer failureReason;
	inputSettings->MapPlayerKey(args, failureReason);
	inputSettings->SaveSettings();

	if (!failureReason.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("OptionsWidget could not bind %s to %s: %s"), *key.ToString(), *mappingName.ToString(), *failureReason.ToString());
	}

	rebuildKeyBindingRows();
	return true;
}

int32 UOptionsWidget::clearConflictingBindings(UEnhancedInputUserSettings* inputSettings, const FKey& key, FName keepMappingName, EPlayerMappableKeySlot keepSlot) const
{
	if (inputSettings == nullptr)
	{
		return 0;
	}

	UEnhancedPlayerMappableKeyProfile* profile = inputSettings->GetActiveKeyProfile();
	if (profile == nullptr)
	{
		return 0;
	}

	// Gathered before anything is unmapped, because unmapping edits the very rows this walk is reading.
	TArray<FMapPlayerKeyArgs> conflicts;

	for (const TPair<FName, FKeyMappingRow>& row : profile->GetPlayerMappingRows())
	{
		for (const FPlayerKeyMapping& mapping : row.Value.Mappings)
		{
			if (mapping.GetCurrentKey() != key)
			{
				continue;
			}

			// The slot being rebound is about to be overwritten anyway, so it never conflicts with itself.
			if ((mapping.GetMappingName() == keepMappingName) && (mapping.GetSlot() == keepSlot))
			{
				continue;
			}

			// Mapped to an invalid key rather than unmapped. Unmapping restores the mapping context default,
			// so a conflict against a default binding would come straight back and nothing would look cleared.
			FMapPlayerKeyArgs args;
			args.MappingName = mapping.GetMappingName();
			args.Slot = mapping.GetSlot();
			args.NewKey = EKeys::Invalid;

			// The lookup inside MapPlayerKey matches on slot and hardware device together,
			// so leaving the device unset finds nothing and quietly adds a second mapping instead of clearing this one.
			args.HardwareDeviceId = mapping.GetHardwareDeviceId().HardwareDeviceIdentifier;

			conflicts.Add(args);
		}
	}

	for (const FMapPlayerKeyArgs& args : conflicts)
	{
		FGameplayTagContainer failureReason;
		inputSettings->MapPlayerKey(args, failureReason);

		if (!failureReason.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("OptionsWidget could not clear %s from %s: %s"), *key.ToString(), *args.MappingName.ToString(), *failureReason.ToString());
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("OptionsWidget cleared %s from %s, which now has no key in that slot."), *key.ToString(), *args.MappingName.ToString());
	}

	return conflicts.Num();
}

void UOptionsWidget::applySettings()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	_resolutionBeforeApply = settings->GetScreenResolution();
	_windowModeBeforeApply = settings->GetFullscreenMode();

	if (resolutionCombo != nullptr)
	{
		const TArray<FIntPoint> resolutions = UGameSettings::getSupportedResolutions();
		const int32 selected = resolutionCombo->GetSelectedIndex();
		if (resolutions.IsValidIndex(selected))
		{
			settings->SetScreenResolution(resolutions[selected]);
		}
	}

	if (windowModeCombo != nullptr)
	{
		settings->setClientWindowMode((windowModeCombo->GetSelectedIndex() == 0) ? EClientWindowMode::Windowed : EClientWindowMode::BorderlessFullscreen);
	}

	if (frameCapCombo != nullptr)
	{
		const int32 selected = frameCapCombo->GetSelectedIndex();
		if (selected == 0)
		{
			settings->SetFrameRateLimit(static_cast<float>(UGameSettings::getNativeRefreshRate()));
		}
		else if (frameCapChoices.IsValidIndex(selected - 1))
		{
			settings->SetFrameRateLimit(static_cast<float>(frameCapChoices[selected - 1]));
		}
	}

	// The tier writes all eleven buckets, so it runs before them and they overwrite it where they differ.
	// Custom is not a level the engine has, so choosing it changes nothing and leaves the buckets alone.
	if ((qualityCombo != nullptr) && (qualityCombo->GetSelectedIndex() < customQualityIndex))
	{
		settings->SetOverallScalabilityLevel(FMath::Clamp(qualityCombo->GetSelectedIndex(), 0, 4));
	}

	applyBucket(viewDistanceCombo, [settings](int32 level) { settings->SetViewDistanceQuality(level); });
	applyBucket(shadowCombo, [settings](int32 level) { settings->SetShadowQuality(level); });
	applyBucket(globalIlluminationCombo, [settings](int32 level) { settings->SetGlobalIlluminationQuality(level); });
	applyBucket(reflectionCombo, [settings](int32 level) { settings->SetReflectionQuality(level); });
	applyBucket(antiAliasingCombo, [settings](int32 level) { settings->SetAntiAliasingQuality(level); });
	applyBucket(textureCombo, [settings](int32 level) { settings->SetTextureQuality(level); });
	applyBucket(effectsCombo, [settings](int32 level) { settings->SetVisualEffectQuality(level); });
	applyBucket(postProcessingCombo, [settings](int32 level) { settings->SetPostProcessingQuality(level); });
	applyBucket(foliageCombo, [settings](int32 level) { settings->SetFoliageQuality(level); });
	applyBucket(shadingCombo, [settings](int32 level) { settings->SetShadingQuality(level); });
	applyBucket(landscapeCombo, [settings](int32 level) { settings->SetLandscapeQuality(level); });

	if (upscalerCombo != nullptr)
	{
		const int32 selected = upscalerCombo->GetSelectedIndex();
		if ((selected >= 0) && (selected < UE_ARRAY_COUNT(upscalerChoices)))
		{
			settings->setUpscalerMethod(upscalerChoices[selected].method);
		}
	}

	if (screenPercentageSlider != nullptr)
	{
		settings->SetResolutionScaleValueEx(screenPercentageSlider->GetValue());
	}

	if (naniteCheck != nullptr)
	{
		settings->setNaniteEnabled(naniteCheck->IsChecked());
	}

	if (lumenCheck != nullptr)
	{
		settings->setLumenEnabled(lumenCheck->IsChecked());
	}

	if (vsyncCheck != nullptr)
	{
		settings->SetVSyncEnabled(vsyncCheck->IsChecked());
	}

	if (showFpsCheck != nullptr)
	{
		settings->setShowFps(showFpsCheck->IsChecked());
	}

	if (showStatsCheck != nullptr)
	{
		settings->setShowStats(showStatsCheck->IsChecked());
	}

	if (fovSlider != nullptr)
	{
		settings->setFieldOfView(fovSlider->GetValue());
	}

	if (masterVolumeSlider != nullptr)
	{
		settings->setMasterVolume(masterVolumeSlider->GetValue());
	}

	if (musicVolumeSlider != nullptr)
	{
		settings->setMusicVolume(musicVolumeSlider->GetValue());
	}

	if (sfxVolumeSlider != nullptr)
	{
		settings->setSfxVolume(sfxVolumeSlider->GetValue());
	}

	if (mouseSensitivitySlider != nullptr)
	{
		settings->setMouseSensitivity(mouseSensitivitySlider->GetValue());
	}

	if (hudScaleSlider != nullptr)
	{
		settings->setHudScale(hudScaleSlider->GetValue());
	}

	if (cameraModeCombo != nullptr)
	{
		settings->setCameraMode((cameraModeCombo->GetSelectedIndex() == 1) ? ECameraMode::FirstPerson : ECameraMode::ThirdPerson);
	}

	if (nameplatesCheck != nullptr)
	{
		settings->setShowNameplates(nameplatesCheck->IsChecked());
	}

	if (damageNumbersCheck != nullptr)
	{
		settings->setShowDamageNumbers(damageNumbersCheck->IsChecked());
	}

	if (chatFilterCheck != nullptr)
	{
		settings->setFilterChatProfanity(chatFilterCheck->IsChecked());
	}

	if (languageCombo != nullptr)
	{
		const TArray<FString> cultures = UGameSettings::getAvailableCultures();
		const int32 selected = languageCombo->GetSelectedIndex();
		if (cultures.IsValidIndex(selected))
		{
			settings->setCultureCode(cultures[selected]);
		}
	}

	const bool displayChanged = (settings->GetScreenResolution() != _resolutionBeforeApply) || (settings->GetFullscreenMode() != _windowModeBeforeApply);

	settings->ApplySettings(false);
	settings->SaveSettings();

	if (displayChanged)
	{
		beginResolutionCountdown();
	}
}

void UOptionsWidget::revertSettings()
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	settings->LoadSettings(true);
	settings->ApplySettings(false);

	populateDisplay();
	populateAudio();
	populateInput();
	populateGameplay();
	populateInterface();
}

void UOptionsWidget::restoreDefaults()
{
	restoreDefaultKeyBindings();

	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	settings->SetToDefaults();
	settings->ApplySettings(false);
	settings->SaveSettings();

	populateDisplay();
	populateAudio();
	populateInput();
	populateGameplay();
	populateInterface();
}

void UOptionsWidget::beginResolutionCountdown()
{
	_revertSecondsLeft = resolutionRevertSeconds;

	if (revertCountdownText != nullptr)
	{
		revertCountdownText->SetVisibility(ESlateVisibility::Visible);
	}

	if (keepChangesButton != nullptr)
	{
		keepChangesButton->SetVisibility(ESlateVisibility::Visible);
	}
}

void UOptionsWidget::finishResolutionCountdown(bool keepChanges)
{
	_revertSecondsLeft = 0.0f;

	if (revertCountdownText != nullptr)
	{
		revertCountdownText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (keepChangesButton != nullptr)
	{
		keepChangesButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (keepChanges)
	{
		return;
	}

	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	// Nobody confirmed, so the display is presumed unreadable and goes back to what worked.
	settings->SetScreenResolution(_resolutionBeforeApply);
	settings->SetFullscreenMode(_windowModeBeforeApply);
	settings->ApplySettings(false);
	settings->SaveSettings();

	populateDisplay();
}

void UOptionsWidget::handleDisplayTabClicked()
{
	playClickSound();

	if (categorySwitcher != nullptr)
	{
		categorySwitcher->SetActiveWidgetIndex(0);
	}
}

void UOptionsWidget::handleAudioTabClicked()
{
	playClickSound();

	if (categorySwitcher != nullptr)
	{
		categorySwitcher->SetActiveWidgetIndex(1);
	}
}

void UOptionsWidget::handleInputTabClicked()
{
	playClickSound();

	if (categorySwitcher != nullptr)
	{
		categorySwitcher->SetActiveWidgetIndex(2);
	}
}

void UOptionsWidget::showCategory(int32 index)
{
	if (categorySwitcher != nullptr)
	{
		categorySwitcher->SetActiveWidgetIndex(index);
	}
}

void UOptionsWidget::handleGameplayTabClicked()
{
	playClickSound();

	if (categorySwitcher != nullptr)
	{
		categorySwitcher->SetActiveWidgetIndex(3);
	}
}

void UOptionsWidget::handleInterfaceTabClicked()
{
	playClickSound();

	if (categorySwitcher != nullptr)
	{
		categorySwitcher->SetActiveWidgetIndex(4);
	}
}

void UOptionsWidget::handleApplyClicked()
{
	playClickSound();

	applySettings();
}

void UOptionsWidget::handleRevertClicked()
{
	playClickSound();

	revertSettings();
}

void UOptionsWidget::handleDefaultsClicked()
{
	playClickSound();

	// Defaults throws away every setting and every key binding at once,
	// which is the one action on this screen with nothing to undo it.
	// Apply is left alone, since the resolution countdown already covers it.
	UGameInstance* gameInstance = GetGameInstance();
	UUISubsystem* subsystem = (gameInstance != nullptr) ? gameInstance->GetSubsystem<UUISubsystem>() : nullptr;
	if (subsystem == nullptr)
	{
		restoreDefaults();
		return;
	}

	subsystem->askConfirm(LocalizedText::get(TEXT("options.reset_confirm")), FOnConfirmAnswered::CreateWeakLambda(this, [this](bool confirmed)
	{
		if (confirmed)
		{
			restoreDefaults();
		}
	}));
}

void UOptionsWidget::handleBackClicked()
{
	playClickSound();

	DeactivateWidget();
}

void UOptionsWidget::handleKeepChangesClicked()
{
	playClickSound();

	finishResolutionCountdown(true);
}

void UOptionsWidget::handleFovChanged(float value)
{
	if (fovValueText != nullptr)
	{
		fovValueText->SetText(FText::AsNumber(FMath::RoundToInt(value)));
	}
}

void UOptionsWidget::handleMouseSensitivityChanged(float value)
{
	if (mouseSensitivityValueText != nullptr)
	{
		mouseSensitivityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), value)));
	}
}

void UOptionsWidget::handleScreenPercentageChanged(float value)
{
	if (screenPercentageValueText != nullptr)
	{
		screenPercentageValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(value))));
	}
}

void UOptionsWidget::handleHudScaleChanged(float value)
{
	if (hudScaleValueText != nullptr)
	{
		hudScaleValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), value)));
	}
}

void UOptionsWidget::handleMasterVolumeChanged(float value)
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	// Volumes take effect while dragging, otherwise there is nothing to judge the level against.
	settings->setMasterVolume(value);
	settings->applyAudioNow();
}

void UOptionsWidget::handleMusicVolumeChanged(float value)
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	settings->setMusicVolume(value);
	settings->applyAudioNow();
}

void UOptionsWidget::handleSfxVolumeChanged(float value)
{
	UGameSettings* settings = UGameSettings::get();
	if (settings == nullptr)
	{
		return;
	}

	settings->setSfxVolume(value);
	settings->applyAudioNow();

	// The effects slider plays its own sound so the level can be heard.
	playClickSound();
}

void UOptionsWidget::playClickSound() const
{
	if (clickSound == nullptr)
	{
		return;
	}

	const UGameSettings* settings = UGameSettings::get();
	const float volume = (settings != nullptr) ? settings->getEffectiveSfxVolume() : 1.0f;

	UGameplayStatics::PlaySound2D(this, clickSound, volume);
}
