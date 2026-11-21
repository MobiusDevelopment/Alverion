#pragma once

#include "CoreMinimal.h"

/**
 * Reads the HTML of a townsperson's dialog into lines of runs a widget can lay out.
 * One reader serves every dialog: it knows tags, never a particular townsperson or page.
 * It reads text, line breaks, paragraphs, centering, font colours, a title, tables as plain lines, character entities, and the links and buttons that send a command or ask for another page.
 * It also reads entry boxes, multi-line boxes and drop-down lists, whose values fill the $name variables in a command, and images, which are read as their size only.
 * It ignores everything else, such as bold and italic.
 */
namespace NpcHtml
{
	enum class ELinkKind : uint8
	{
		None,

		/** Sends a command to the server, as RequestBypassToServer. */
		Bypass,

		/** Asks the server for another page by file name, as RequestLinkHtml. */
		Page
	};

	enum class ERunKind : uint8
	{
		/** Text, which may be the text of a link or a button. */
		Text,

		/** A one-line entry box. */
		Edit,

		/** A multi-line entry box. */
		MultiEdit,

		/** A drop-down list. */
		ComboBox,

		/** An image, kept as a blank of its size. */
		Image
	};

	/** A stretch of a line that reads the same way throughout: plain text, the text of one link or button, or one entry box, list or image. */
	struct ALVERION_API FRun
	{
		ERunKind kind = ERunKind::Text;
		FString text;

		bool hasColor = false;
		FLinearColor color = FLinearColor::White;

		ELinkKind linkKind = ELinkKind::None;

		/** The command or the file name, exactly as the page wrote it, because the server only accepts what it sent. */
		FString linkTarget;

		/** The name of an entry box or list, which a command names as $name to take its value. */
		FString fieldName;

		/** The choices of a drop-down list, in the page's order. */
		TArray<FString> options;

		/** The size the page gave an entry box, a list or an image, zero where it gave none. */
		float width = 0.0f;
		float height = 0.0f;
	};

	struct ALVERION_API FLine
	{
		TArray<FRun> runs;
		bool centered = false;
	};

	struct ALVERION_API FDocument
	{
		/** The page's title, empty when it has none. It is not part of the lines. */
		FString title;

		/** An empty line is a deliberate gap, including at the start or end of the page. */
		TArray<FLine> lines;
	};

	ALVERION_API FDocument parse(const FString& html);

	/**
	 * Puts the value of each field into the command where it is named as $name, which is how the server's pages send what was typed.
	 * A name that is not a field of the page becomes nothing, so no $ is ever sent.
	 */
	ALVERION_API FString substituteFields(const FString& command, const TMap<FString, FString>& values);
}
