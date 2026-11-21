#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"
#include "UI/NpcDialogWidget.h"
#include "UI/NpcHtml.h"

namespace
{
	// The text of a line, runs joined.
	FString npcHtmlTestTextOf(const NpcHtml::FLine& line)
	{
		FString text;
		for (const NpcHtml::FRun& run : line.runs)
		{
			text.Append(run.text);
		}

		return text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcHtmlTest, "Alverion.UI.NpcHtml", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNpcHtmlTest::RunTest(const FString& parameters)
{
	// A page of the kind the server sends: title, centred heading, colours, a paragraph, links of both kinds, a button, entities and a table.
	const FString page = TEXT(
		"<html><head><title>  Gatekeeper  </title></head>\n"
		"<body>\n"
		"<center><font color=\"LEVEL\">Welcome</font></center><br>\n"
		"Hello,\n   traveller! 1 &lt; 2 &amp;&nbsp;3 &#65;&#x42;<br1>\n"
		"<a action=\"bypass -h npc_268480123_Quest\">Quest</a><br>\n"
		"<a action=\"link default/30006-1.htm\">Next page</a><br>\n"
		"<br>\n"
		"<font color=\"FF0000\">Red</font> and plain<p>\n"
		"<button value=\"Buy\" action=\"bypass -h npc_1_Buy 7\" width=100 height=20><br>\n"
		"<a action=\"bypass -h npc_1_Say $text\">Needs a box</a><br>\n"
		"<table><tr><td>Name</td><td>Cost</td></tr><tr><td>Sword</td><td>100</td></tr></table>\n"
		"<!-- a comment --><script>ignored()</script>Done</body></html>");

	const NpcHtml::FDocument document = NpcHtml::parse(page);
	TestEqual(TEXT("The title is kept apart from the lines"), document.title, FString(TEXT("Gatekeeper")));

	if (!TestEqual(TEXT("Lines"), document.lines.Num(), 11))
	{
		return false;
	}

	// The heading is centred and coloured.
	const NpcHtml::FLine& heading = document.lines[0];
	TestEqual(TEXT("Heading text"), npcHtmlTestTextOf(heading), FString(TEXT("Welcome")));
	TestTrue(TEXT("Heading is centred"), heading.centered);
	TestTrue(TEXT("Heading has a colour"), heading.runs[0].hasColor);

	// Whitespace collapses, entities decode, and a numbered line break ends the line.
	TestEqual(TEXT("Entities and collapsed blanks"), npcHtmlTestTextOf(document.lines[1]), FString(TEXT("Hello, traveller! 1 < 2 & 3 AB")));
	TestFalse(TEXT("Plain text is not centred"), document.lines[1].centered);
	TestFalse(TEXT("Plain text has no colour"), document.lines[1].runs[0].hasColor);

	// Both kinds of link, with the command exactly as the page wrote it after the -h.
	TestEqual(TEXT("Bypass link kind"), document.lines[2].runs[0].linkKind, NpcHtml::ELinkKind::Bypass);
	TestEqual(TEXT("Bypass link command"), document.lines[2].runs[0].linkTarget, FString(TEXT("npc_268480123_Quest")));
	TestEqual(TEXT("Bypass link text"), npcHtmlTestTextOf(document.lines[2]), FString(TEXT("Quest")));
	TestEqual(TEXT("Page link kind"), document.lines[3].runs[0].linkKind, NpcHtml::ELinkKind::Page);
	TestEqual(TEXT("Page link file"), document.lines[3].runs[0].linkTarget, FString(TEXT("default/30006-1.htm")));

	// Two breaks in a row leave one empty line.
	TestEqual(TEXT("A deliberate gap"), document.lines[4].runs.Num(), 0);

	// A colour ends with its tag, and a paragraph ends the line.
	if (TestEqual(TEXT("Runs of the coloured line"), document.lines[5].runs.Num(), 2))
	{
		TestTrue(TEXT("Red run is coloured"), document.lines[5].runs[0].hasColor);
		TestEqual(TEXT("Red run keeps the space that followed it"), document.lines[5].runs[0].text, FString(TEXT("Red ")));
		TestFalse(TEXT("The rest is not coloured"), document.lines[5].runs[1].hasColor);
		TestEqual(TEXT("The rest"), document.lines[5].runs[1].text, FString(TEXT("and plain")));
	}

	// A button is a link whose text is its value.
	TestEqual(TEXT("Button text"), npcHtmlTestTextOf(document.lines[6]), FString(TEXT("Buy")));
	TestEqual(TEXT("Button kind"), document.lines[6].runs[0].linkKind, NpcHtml::ELinkKind::Bypass);
	TestEqual(TEXT("Button command"), document.lines[6].runs[0].linkTarget, FString(TEXT("npc_1_Buy 7")));

	// A link whose command names a field is a link like any other, and the command keeps its $name until it is sent.
	TestEqual(TEXT("A link naming a field is a link"), document.lines[7].runs[0].linkKind, NpcHtml::ELinkKind::Bypass);
	TestEqual(TEXT("Its command keeps the name"), document.lines[7].runs[0].linkTarget, FString(TEXT("npc_1_Say $text")));
	TestEqual(TEXT("Its text stays"), npcHtmlTestTextOf(document.lines[7]), FString(TEXT("Needs a box")));

	// Table rows are lines, with their cells a gap apart; comments and scripts are skipped.
	TestEqual(TEXT("Table header row"), npcHtmlTestTextOf(document.lines[8]), FString(TEXT("Name    Cost")));
	TestEqual(TEXT("Table row"), npcHtmlTestTextOf(document.lines[9]), FString(TEXT("Sword    100")));
	TestEqual(TEXT("Text after a comment and a script"), npcHtmlTestTextOf(document.lines[10]), FString(TEXT("Done")));

	// Odd input is read rather than refused: a stray bracket is text, an unknown entity is kept as written, and nothing is a link without an action.
	const NpcHtml::FDocument odd = NpcHtml::parse(TEXT("1 < 2 &unknown; <a href=\"x\">plain</a> <b>bold</b>"));
	if (TestEqual(TEXT("Odd input is one line"), odd.lines.Num(), 1))
	{
		TestEqual(TEXT("Odd input text"), npcHtmlTestTextOf(odd.lines[0]), FString(TEXT("1 < 2 &unknown; plain bold")));
		TestEqual(TEXT("A link without an action is plain"), odd.lines[0].runs.Num(), 1);
	}

	// Fields and images: found with their names, sizes and choices, in the order the page has them.
	const NpcHtml::FDocument form = NpcHtml::parse(TEXT(
		"<html><body>Name <edit var=\"who\" width=120 height=15 type=char><br>"
		"<multiedit var=\"memo\" width=\"200\" height=\"100\" /><br>"
		"<combobox var=\"kind\" list=\"a;b;;c\" width=90><br>"
		"<img src=\"Some.Texture\" width=270 height=5><br>"
		"<img src=\"Some.Texture\" width=1 height=3>"
		"<button value=\"Go\" action=\"bypass -h npc_1_Go $who $kind $unknown\"></body></html>"));
	if (TestEqual(TEXT("Form lines"), form.lines.Num(), 5))
	{
		const NpcHtml::FLine& nameLine = form.lines[0];
		if (TestEqual(TEXT("A label and its box share a line"), nameLine.runs.Num(), 2))
		{
			TestEqual(TEXT("The label"), nameLine.runs[0].text, FString(TEXT("Name ")));
			TestTrue(TEXT("The entry box"), nameLine.runs[1].kind == NpcHtml::ERunKind::Edit);
			TestEqual(TEXT("Entry box name"), nameLine.runs[1].fieldName, FString(TEXT("who")));
			TestEqual(TEXT("Entry box width"), nameLine.runs[1].width, 120.0f);
		}

		TestTrue(TEXT("The multi-line box"), form.lines[1].runs[0].kind == NpcHtml::ERunKind::MultiEdit);
		TestEqual(TEXT("Multi-line box name"), form.lines[1].runs[0].fieldName, FString(TEXT("memo")));
		TestEqual(TEXT("Multi-line box height"), form.lines[1].runs[0].height, 100.0f);

		const NpcHtml::FRun& list = form.lines[2].runs[0];
		TestTrue(TEXT("The drop-down list"), list.kind == NpcHtml::ERunKind::ComboBox);
		TestEqual(TEXT("List name"), list.fieldName, FString(TEXT("kind")));
		TestEqual(TEXT("List choices, empty ones dropped"), FString::Join(list.options, TEXT(",")), FString(TEXT("a,b,c")));

		// An image is its size and nothing else, whatever texture it names.
		TestTrue(TEXT("An image"), form.lines[3].runs[0].kind == NpcHtml::ERunKind::Image);
		TestEqual(TEXT("Image width"), form.lines[3].runs[0].width, 270.0f);
		TestEqual(TEXT("Image height"), form.lines[3].runs[0].height, 5.0f);
		TestTrue(TEXT("An image has no text"), form.lines[3].runs[0].text.IsEmpty());
		TestEqual(TEXT("An image beside a button"), form.lines[4].runs.Num(), 2);

		// The button's command takes the fields' values where it names them, and nothing where it names no field.
		TMap<FString, FString> values;
		values.Add(TEXT("who"), TEXT("Ann"));
		values.Add(TEXT("kind"), TEXT("b"));
		TestEqual(TEXT("Fields fill the command"), NpcHtml::substituteFields(form.lines[4].runs[1].linkTarget, values), FString(TEXT("npc_1_Go Ann b ")));
	}

	TestEqual(TEXT("A name ends at its last letter or digit"), NpcHtml::substituteFields(TEXT("say $who_1,$who."), TMap<FString, FString>({ { TEXT("who_1"), TEXT("X") }, { TEXT("who"), TEXT("Y") } })), FString(TEXT("say X,Y.")));
	// The tutorial page as the server sends it: the command is a token, and the last line is a link.
	const NpcHtml::FDocument tutorial = NpcHtml::parse(TEXT(
		"<html><body>Grand Master Roien:<br>\r\n"
		"First, you must learn the basic methods of control in the game. You should meet the <font color=\"LEVEL\">Newbie Helper</font> over there.<br>\r\n"
		"Listen carefully to what he tells you, you will find it very useful later in the game.<br>\r\n"
		"<a action=\"bypass FDDF8E98\">Quest</a>\r\n"
		"</body></html>"));
	if (TestEqual(TEXT("Tutorial lines"), tutorial.lines.Num(), 4))
	{
		TestEqual(TEXT("The tutorial link text"), npcHtmlTestTextOf(tutorial.lines[3]), FString(TEXT("Quest")));
		TestEqual(TEXT("The tutorial link kind"), tutorial.lines[3].runs[0].linkKind, NpcHtml::ELinkKind::Bypass);
		TestEqual(TEXT("The tutorial link command"), tutorial.lines[3].runs[0].linkTarget, FString(TEXT("FDDF8E98")));
	}

	TestEqual(TEXT("A page with only HTML whitespace has no lines"), NpcHtml::parse(TEXT("<html><body> \r\n </body></html>")).lines.Num(), 0);
	TestEqual(TEXT("A page with only a break keeps its blank line"), NpcHtml::parse(TEXT("<html><body><br> </body></html>")).lines.Num(), 1);
	const NpcHtml::FDocument padded = NpcHtml::parse(TEXT("<html><body><script>\"<br>\"</script><br>First<br><br>Last<br><br></body></html>"));
	if (TestEqual(TEXT("Explicit blank lines survive at both edges and between lines"), padded.lines.Num(), 5))
	{
		TestTrue(TEXT("The leading blank line survives"), padded.lines[0].runs.IsEmpty());
		TestTrue(TEXT("The middle blank line survives"), padded.lines[2].runs.IsEmpty());
		TestTrue(TEXT("The trailing blank line survives"), padded.lines[4].runs.IsEmpty());
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcDialogLayoutTest, "Alverion.UI.NpcDialogLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNpcDialogLayoutTest::RunTest(const FString& parameters)
{
	UNpcDialogWidget* dialog = NewObject<UNpcDialogWidget>();
	dialog->Initialize();
	dialog->showPage(TEXT("<html><head><title>Guard</title></head><body>Be vigilant, traveller.<br><br>Report anyone suspicious.<br><a action=\"bypass npc_1_Quest\">Quest</a><br><a action=\"link default/guard.htm\">Next page</a></body></html>"));
	const TSharedRef<SWidget> widget = dialog->TakeWidget();
	widget->SlatePrepass(1.0f);

	const FVector2D size = dialog->GetDesiredSize();
	TestTrue(TEXT("The conversation window has portrait proportions"), (size.X > 0.0) && (size.Y > size.X));

	UScrollBox* content = nullptr;
	bool speakerNamed = false;
	dialog->WidgetTree->ForEachWidget([&content, &speakerNamed](UWidget* widget)
	{
		if (UScrollBox* scroll = Cast<UScrollBox>(widget))
		{
			content = scroll;
		}

		if (const UTextBlock* label = Cast<UTextBlock>(widget))
		{
			speakerNamed = speakerNamed || (label->GetText().ToString() == TEXT("Guard"));
		}
	});
	TestTrue(TEXT("The HTML title appears in the header"), speakerNamed);
	if (!TestNotNull(TEXT("The page can scroll"), content))
	{
		return false;
	}

	if (TestEqual(TEXT("The scrolling page keeps both paragraphs, their gap and both choices"), content->GetChildrenCount(), 5))
	{
		const UTextBlock* greeting = Cast<UTextBlock>(content->GetChildAt(0));
		if (TestNotNull(TEXT("The greeting appears first"), greeting))
		{
			TestEqual(TEXT("The greeting is preserved"), greeting->GetText().ToString(), FString(TEXT("Be vigilant, traveller.")));
		}

		TestNotNull(TEXT("The deliberate blank line keeps its space"), Cast<USizeBox>(content->GetChildAt(1)));
		TestNotNull(TEXT("The line after the gap remains separate"), Cast<UTextBlock>(content->GetChildAt(2)));
		TestNotNull(TEXT("Quest gets its own button row"), Cast<UButton>(content->GetChildAt(3)));
		TestNotNull(TEXT("The next page link gets its own button row"), Cast<UButton>(content->GetChildAt(4)));
	}

	dialog->showPage(TEXT("<html><head><title>Gatekeeper</title></head><body>Destination:<br>Choose a town.</body></html>"));
	const UTextBlock* first = Cast<UTextBlock>(content->GetChildAt(0));
	if (TestNotNull(TEXT("The explicit title leaves the first body line in place"), first))
	{
		TestEqual(TEXT("The first body line retains its punctuation"), first->GetText().ToString(), FString(TEXT("Destination:")));
		const UScrollBoxSlot* lineSlot = CastChecked<UScrollBoxSlot>(first->Slot);
		TestTrue(TEXT("An authored break is spaced apart from ordinary wrapping"), lineSlot->GetPadding().Bottom > 0.0f);
	}

	FString longPage = TEXT("<html><head><title>Guard</title></head><body>");
	for (int32 line = 0; line < 64; line++)
	{
		longPage.Append(TEXT("Be vigilant, traveller.<br>"));
	}

	longPage.Append(TEXT("</body></html>"));
	dialog->showPage(longPage);
	dialog->ForceLayoutPrepass();
	TestEqual(TEXT("Long pages keep the same frame size"), dialog->GetDesiredSize(), size);
	TestEqual(TEXT("Every line stays in the scroll box"), content->GetChildrenCount(), 64);
	return true;
}

#endif
