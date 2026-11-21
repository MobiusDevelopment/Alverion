#include "UI/NpcHtml.h"

namespace NpcHtml
{
namespace
{
	// Colours the pages name rather than spell. Anything not here is read as hexadecimal, and anything not hexadecimal leaves the colour as it was.
	struct FNamedColor
	{
		const TCHAR* name;
		FColor color;
	};

	const FNamedColor npcHtmlNamedColors[] =
	{
		{ TEXT("white"), FColor(255, 255, 255) },
		{ TEXT("black"), FColor(0, 0, 0) },
		{ TEXT("red"), FColor(255, 0, 0) },
		{ TEXT("green"), FColor(0, 255, 0) },
		{ TEXT("blue"), FColor(0, 0, 255) },
		{ TEXT("yellow"), FColor(255, 255, 0) },
		{ TEXT("gray"), FColor(128, 128, 128) },
		{ TEXT("grey"), FColor(128, 128, 128) },
		{ TEXT("orange"), FColor(255, 165, 0) },
		{ TEXT("level"), FColor(244, 214, 117) }
	};

	bool readColor(const FString& value, FLinearColor& color)
	{
		FString text = value.TrimStartAndEnd();
		for (const FNamedColor& named : npcHtmlNamedColors)
		{
			if (text.Equals(named.name, ESearchCase::IgnoreCase))
			{
				color = FLinearColor::FromSRGBColor(named.color);
				return true;
			}
		}

		if (text.StartsWith(TEXT("#")))
		{
			text.RightChopInline(1);
		}

		if (text.Len() != 6)
		{
			return false;
		}

		for (const TCHAR letter : text)
		{
			if (!FChar::IsHexDigit(letter))
			{
				return false;
			}
		}

		color = FLinearColor::FromSRGBColor(FColor::FromHex(text));
		return true;
	}

	/** One tag as written: its lower case name, whether it closes, and its attributes with their values left exactly as written. */
	struct FTag
	{
		FString name;
		bool closing = false;
		TMap<FString, FString> attributes;
	};

	/**
	 * Reads the tag that starts at the '<' at the index into the tag.
	 * Returns the index after its '>', or INDEX_NONE when there is no '>' and so no tag.
	 * A quoted value may hold a '>', so quotes are followed.
	 * Values are not decoded: the server keeps the command text it sent and only accepts that, so a client that changed it would be refused.
	 */
	int32 readTag(const FString& html, int32 start, FTag& tag)
	{
		int32 end = INDEX_NONE;
		TCHAR quote = 0;
		for (int32 index = start + 1; index < html.Len(); index++)
		{
			const TCHAR letter = html[index];
			if (quote != 0)
			{
				quote = (letter == quote) ? 0 : quote;
			}
			else if ((letter == TEXT('"')) || (letter == TEXT('\'')))
			{
				quote = letter;
			}
			else if (letter == TEXT('>'))
			{
				end = index;
				break;
			}
		}

		if (end == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		int32 index = start + 1;
		if ((index < end) && (html[index] == TEXT('/')))
		{
			tag.closing = true;
			index++;
		}

		while ((index < end) && FChar::IsAlnum(html[index]))
		{
			tag.name.AppendChar(FChar::ToLower(html[index]));
			index++;
		}

		while (index < end)
		{
			while ((index < end) && (FChar::IsWhitespace(html[index]) || (html[index] == TEXT('/'))))
			{
				index++;
			}

			FString name;
			while ((index < end) && !FChar::IsWhitespace(html[index]) && (html[index] != TEXT('=')) && (html[index] != TEXT('/')))
			{
				name.AppendChar(FChar::ToLower(html[index]));
				index++;
			}

			while ((index < end) && FChar::IsWhitespace(html[index]))
			{
				index++;
			}

			FString value;
			if ((index < end) && (html[index] == TEXT('=')))
			{
				index++;
				while ((index < end) && FChar::IsWhitespace(html[index]))
				{
					index++;
				}

				if ((index < end) && ((html[index] == TEXT('"')) || (html[index] == TEXT('\''))))
				{
					const TCHAR opening = html[index];
					index++;
					while ((index < html.Len()) && (html[index] != opening))
					{
						value.AppendChar(html[index]);
						index++;
					}

					index++;
				}
				else
				{
					while ((index < end) && !FChar::IsWhitespace(html[index]))
					{
						value.AppendChar(html[index]);
						index++;
					}
				}
			}

			if (!name.IsEmpty())
			{
				tag.attributes.Add(name, value);
			}
		}

		return end + 1;
	}

	/**
	 * Reads the action of a link or button.
	 * "bypass -h command" and "bypass command" send the command, "link file" asks for the page, and the server caches the text after the "-h " the same way.
	 * An action it does not know is not a link. A command may name fields as $name, which are filled when it is sent.
	 */
	bool readAction(const FString& action, ELinkKind& kind, FString& target)
	{
		FString text = action.TrimStartAndEnd();
		if (text.StartsWith(TEXT("bypass"), ESearchCase::IgnoreCase))
		{
			kind = ELinkKind::Bypass;
			text.RightChopInline(6);
			text.TrimStartInline();
			if (text.StartsWith(TEXT("-h "), ESearchCase::IgnoreCase))
			{
				text.RightChopInline(3);
				text.TrimStartInline();
			}
		}
		else if (text.StartsWith(TEXT("link "), ESearchCase::IgnoreCase))
		{
			kind = ELinkKind::Page;
			text.RightChopInline(5);
			text.TrimStartInline();
		}
		else
		{
			return false;
		}

		if (text.IsEmpty())
		{
			return false;
		}

		target = text;
		return true;
	}

	/** Reads a character entity that starts at the '&' at the index. Returns the index after it, or INDEX_NONE when it is not one this reader knows. */
	int32 readEntity(const FString& html, int32 start, TCHAR& letter)
	{
		static constexpr int32 longestEntity = 10;

		const int32 end = html.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, start);
		if ((end == INDEX_NONE) || (end - start > longestEntity))
		{
			return INDEX_NONE;
		}

		const FString name = html.Mid(start + 1, end - start - 1);
		if (name == TEXT("lt"))
		{
			letter = TEXT('<');
		}
		else if (name == TEXT("gt"))
		{
			letter = TEXT('>');
		}
		else if (name == TEXT("amp"))
		{
			letter = TEXT('&');
		}
		else if (name == TEXT("quot"))
		{
			letter = TEXT('"');
		}
		else if (name == TEXT("apos"))
		{
			letter = TEXT('\'');
		}
		else if (name == TEXT("nbsp"))
		{
			letter = TEXT(' ');
		}
		else if ((name.Len() > 1) && (name[0] == TEXT('#')))
		{
			const bool hexadecimal = (name[1] == TEXT('x')) || (name[1] == TEXT('X'));
			int32 code = 0;
			for (int32 index = hexadecimal ? 2 : 1; index < name.Len(); index++)
			{
				const TCHAR digit = name[index];
				if (hexadecimal ? !FChar::IsHexDigit(digit) : !FChar::IsDigit(digit))
				{
					return INDEX_NONE;
				}

				code = (code * (hexadecimal ? 16 : 10)) + FChar::ConvertCharDigitToInt(digit);
			}

			if ((code <= 0) || (code > 0xFFFF))
			{
				return INDEX_NONE;
			}

			letter = static_cast<TCHAR>(code);
		}
		else
		{
			return INDEX_NONE;
		}

		return end + 1;
	}

	/** Builds the document as the tags and text go by. */
	class FReader
	{
	public:
		FDocument read(const FString& html)
		{
			int32 index = 0;
			while (index < html.Len())
			{
				if (html[index] == TEXT('<'))
				{
					if (html.Mid(index, 4) == TEXT("<!--"))
					{
						const int32 close = html.Find(TEXT("-->"), ESearchCase::CaseSensitive, ESearchDir::FromStart, index + 4);
						index = (close == INDEX_NONE) ? html.Len() : close + 3;
						continue;
					}

					FTag tag;
					const int32 next = readTag(html, index, tag);
					if ((next != INDEX_NONE) && !tag.name.IsEmpty())
					{
						index = next;
						handleTag(tag);
						continue;
					}
				}

				if (html[index] == TEXT('&'))
				{
					TCHAR letter = 0;
					const int32 next = readEntity(html, index, letter);
					if (next != INDEX_NONE)
					{
						// A character entity is kept as written even when it is a space, which is what the non-breaking one is for.
						addLetter(letter, true);
						index = next;
						continue;
					}
				}

				addLetter(html[index], false);
				index++;
			}

			endLine(false);
			return _document;
		}

	private:
		static bool isLineBreak(const FString& name)
		{
			// br, and the numbered line breaks the server's pages use, such as br1.
			if (!name.StartsWith(TEXT("br")))
			{
				return false;
			}

			for (int32 index = 2; index < name.Len(); index++)
			{
				if (!FChar::IsDigit(name[index]))
				{
					return false;
				}
			}

			return true;
		}

		void handleTag(const FString& name, bool closing, const TMap<FString, FString>& attributes)
		{
			// Text between these is code or style rather than something to read.
			if ((name == TEXT("script")) || (name == TEXT("style")))
			{
				_hidden = !closing;
			}
			else if (name == TEXT("title"))
			{
				_inTitle = !closing;
			}
			else if (isLineBreak(name))
			{
				if (!_hidden && !_inTitle)
				{
					endLine(true);
				}
			}
			else if ((name == TEXT("p")) || (name == TEXT("hr")) || (name == TEXT("table")) || (name == TEXT("tr")) || (name == TEXT("div")))
			{
				endLine(false);
			}
			else if ((name == TEXT("td")) || (name == TEXT("th")))
			{
				// Cells of a row sit side by side, a gap apart, so the end of one is noted for the text that follows it.
				_cellEnded = _cellEnded || closing;
			}
			else if (name == TEXT("center"))
			{
				_centerDepth = FMath::Max(0, _centerDepth + (closing ? -1 : 1));
				if (!closing && _line.runs.IsEmpty())
				{
					_line.centered = true;
				}
			}
			else if (name == TEXT("font"))
			{
				if (closing)
				{
					if (_colors.Num() > 1)
					{
						_colors.Pop();
					}
				}
				else
				{
					TPair<bool, FLinearColor> color = _colors.Last();
					const FString* value = attributes.Find(TEXT("color"));
					FLinearColor read;
					if ((value != nullptr) && readColor(*value, read))
					{
						color = TPair<bool, FLinearColor>(true, read);
					}

					_colors.Add(color);
				}
			}
			else if (name == TEXT("a"))
			{
				_linkKind = ELinkKind::None;
				_linkTarget.Reset();

				const FString* action = attributes.Find(TEXT("action"));
				if (!closing && (action != nullptr) && !readAction(*action, _linkKind, _linkTarget))
				{
					_linkKind = ELinkKind::None;
					_linkTarget.Reset();
				}
			}
			else if (((name == TEXT("edit")) || (name == TEXT("multiedit")) || (name == TEXT("combobox")) || (name == TEXT("img"))) && !closing)
			{
				addElement(name, attributes);
			}
			else if ((name == TEXT("button")) && !closing)
			{
				// A button is a link with its caption in an attribute rather than between tags.
				const FString* value = attributes.Find(TEXT("value"));
				if ((value == nullptr) || value->IsEmpty())
				{
					return;
				}

				ELinkKind kind = ELinkKind::None;
				FString target;
				const FString* action = attributes.Find(TEXT("action"));
				if ((action == nullptr) || !readAction(*action, kind, target))
				{
					kind = ELinkKind::None;
					target.Reset();
				}

				const ELinkKind savedKind = _linkKind;
				const FString savedTarget = _linkTarget;
				_linkKind = kind;
				_linkTarget = target;
				addText(*value);
				_linkKind = savedKind;
				_linkTarget = savedTarget;
			}
		}

		/** An entry box, a list or an image goes into the line as a run of its own. */
		void addElement(const FString& name, const TMap<FString, FString>& attributes)
		{
			FRun run;
			if (name == TEXT("edit"))
			{
				run.kind = ERunKind::Edit;
			}
			else if (name == TEXT("multiedit"))
			{
				run.kind = ERunKind::MultiEdit;
			}
			else if (name == TEXT("combobox"))
			{
				run.kind = ERunKind::ComboBox;
			}
			else
			{
				run.kind = ERunKind::Image;
			}

			// Only the size of an image is read. Its source names a texture of the stock client, which is not ours to use,
			// so the page keeps its layout with a blank of that size, and a mapping to images of our own can come later.
			const FString* var = attributes.Find(TEXT("var"));
			const FString* width = attributes.Find(TEXT("width"));
			const FString* height = attributes.Find(TEXT("height"));
			const FString* list = attributes.Find(TEXT("list"));
			run.fieldName = (var != nullptr) ? var->TrimStartAndEnd() : FString();
			run.width = (width != nullptr) ? FCString::Atof(**width) : 0.0f;
			run.height = (height != nullptr) ? FCString::Atof(**height) : 0.0f;
			if ((list != nullptr) && (run.kind == ERunKind::ComboBox))
			{
				list->ParseIntoArray(run.options, TEXT(";"), true);
			}

			if (!_line.runs.IsEmpty())
			{
				FRun& last = _line.runs.Last();
				if (last.kind == ERunKind::Text)
				{
					last.text.Append(_cellEnded ? TEXT("    ") : (_spacePending ? TEXT(" ") : TEXT("")));
				}
			}

			_spacePending = false;
			_cellEnded = false;
			_line.runs.Add(run);
			_line.centered = _line.centered || (_centerDepth > 0);
		}

		void handleTag(const FTag& tag)
		{
			handleTag(tag.name, tag.closing, tag.attributes);
		}

		void addLetter(TCHAR letter, bool literal)
		{
			if (_hidden)
			{
				return;
			}

			if (_inTitle)
			{
				_document.title.AppendChar(FChar::IsWhitespace(letter) ? TEXT(' ') : letter);
				return;
			}

			if (!literal && FChar::IsWhitespace(letter))
			{
				_spacePending = true;
				return;
			}

			addText(FString(1, &letter));
		}

		/** Adds text to the line in the style in force, after the single space that any run of blanks before it turned into. */
		void addText(const FString& text)
		{
			const bool lineHasText = !_line.runs.IsEmpty();
			const TPair<bool, FLinearColor>& color = _colors.Last();

			if (_cellEnded && lineHasText)
			{
				// A cell boundary is a wider gap than a word's.
				_line.runs.Last().text.Append(TEXT("    "));
				_spacePending = false;
			}

			_cellEnded = false;

			FString shown = text;
			if (_spacePending && lineHasText)
			{
				FRun& last = _line.runs.Last();
				if (sameStyle(last, color))
				{
					shown = TEXT(" ") + shown;
				}
				else
				{
					last.text.AppendChar(TEXT(' '));
				}
			}

			_spacePending = false;

			if (!lineHasText || !sameStyle(_line.runs.Last(), color))
			{
				FRun run;
				run.hasColor = color.Key;
				run.color = color.Value;
				run.linkKind = _linkKind;
				run.linkTarget = _linkTarget;
				_line.runs.Add(run);
			}

			_line.runs.Last().text.Append(shown);
			_line.centered = _line.centered || (_centerDepth > 0);
		}

		bool sameStyle(const FRun& run, const TPair<bool, FLinearColor>& color) const
		{
			return (run.kind == ERunKind::Text) && (run.hasColor == color.Key) && (!color.Key || run.color.Equals(color.Value)) && (run.linkKind == _linkKind) && (run.linkTarget == _linkTarget);
		}

		/** Ends the line. A line with nothing on it is kept only when the page asked for the gap. */
		void endLine(bool explicitBreak)
		{
			_spacePending = false;
			_cellEnded = false;
			if (!_line.runs.IsEmpty() || explicitBreak)
			{
				_document.lines.Add(_line);
			}

			_line = FLine();
		}

		FDocument _document;
		FLine _line;
		bool _spacePending = false;
		bool _cellEnded = false;
		bool _hidden = false;
		bool _inTitle = false;
		int32 _centerDepth = 0;

		/** The font colours in force, innermost last, each with whether a colour was named at all. The first is the page's own. */
		TArray<TPair<bool, FLinearColor>> _colors = { TPair<bool, FLinearColor>(false, FLinearColor::White) };

		ELinkKind _linkKind = ELinkKind::None;
		FString _linkTarget;
	};
}

FString substituteFields(const FString& command, const TMap<FString, FString>& values)
{
	FString result;
	int32 index = 0;
	while (index < command.Len())
	{
		if (command[index] != TEXT('$'))
		{
			result.AppendChar(command[index]);
			index++;
			continue;
		}

		FString name;
		index++;
		while ((index < command.Len()) && (FChar::IsAlnum(command[index]) || (command[index] == TEXT('_'))))
		{
			name.AppendChar(command[index]);
			index++;
		}

		const FString* value = values.Find(name);
		if (value != nullptr)
		{
			result.Append(*value);
		}
	}

	return result;
}

FDocument parse(const FString& html)
{
	FReader reader;
	FDocument document = reader.read(html);
	document.title.TrimStartAndEndInline();
	return document;
}
}
