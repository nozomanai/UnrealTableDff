#include "STableDiffRevisionMenu.h"

#include "STableDiff.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "ISourceControlModule.h"
#include "ISourceControlRevision.h"
#include "ISourceControlState.h"
#include "SourceControlHelpers.h"
#include "SourceControlOperations.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STableDiffRevisionMenu"

STableDiffRevisionMenu::~STableDiffRevisionMenu()
{
	if (QueryState == EQueryState::InProgress && SourceControlQueryOp.IsValid())
	{
		ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
		if (Provider.CanCancelOperation(SourceControlQueryOp.ToSharedRef()))
		{
			Provider.CancelOperation(SourceControlQueryOp.ToSharedRef());
		}
	}
}

void STableDiffRevisionMenu::Construct(const FArguments& InArgs, UDataTable* InTable)
{
	Table = InTable;
	QueryState = EQueryState::Idle;

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(460.f)
		[
			SAssignNew(MenuBox, SVerticalBox)
		]
	];

	if (InTable == nullptr)
	{
		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectOne", "Select a single Data Table."))
			.AutoWrapText(true)
		];
		return;
	}

	if (ISourceControlModule::Get().IsEnabled() == false || ISourceControlModule::Get().GetProvider().IsAvailable() == false)
	{
		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RevisionControlDisabled", "Revision control is disabled."))
			.AutoWrapText(true)
		];
		return;
	}

	Filename = USourceControlHelpers::PackageFilename(InTable->GetOutermost());

	MenuBox->AddSlot()
	.AutoHeight()
	[
		SNew(SBorder)
		.Visibility(this, &STableDiffRevisionMenu::GetInProgressVisibility)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SThrobber)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UpdatingHistory", "Updating history..."))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Visibility(this, &STableDiffRevisionMenu::GetCancelButtonVisibility)
				.OnClicked(this, &STableDiffRevisionMenu::OnCancelButtonClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Cancel", "Cancel"))
				]
			]
		]
	];

	SourceControlQueryOp = ISourceControlOperation::Create<FUpdateStatus>();
	SourceControlQueryOp->SetUpdateHistory(true);
	QueryState = EQueryState::InProgress;
	ISourceControlModule::Get().GetProvider().Execute(
		SourceControlQueryOp.ToSharedRef(),
		Filename,
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateSP(this, &STableDiffRevisionMenu::OnSourceControlQueryComplete));
}

EVisibility STableDiffRevisionMenu::GetInProgressVisibility() const
{
	if (QueryState == EQueryState::InProgress)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

EVisibility STableDiffRevisionMenu::GetCancelButtonVisibility() const
{
	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
	if (SourceControlQueryOp.IsValid() && Provider.CanCancelOperation(SourceControlQueryOp.ToSharedRef()))
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

FReply STableDiffRevisionMenu::OnCancelButtonClicked() const
{
	if (SourceControlQueryOp.IsValid())
	{
		ISourceControlModule::Get().GetProvider().CancelOperation(SourceControlQueryOp.ToSharedRef());
	}
	return FReply::Handled();
}

void STableDiffRevisionMenu::OnSourceControlQueryComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	if (SourceControlQueryOp.IsValid() == false || SourceControlQueryOp != InOperation)
	{
		return;
	}

	PopulateRevisionList(InResult);
	SourceControlQueryOp.Reset();
	QueryState = EQueryState::Complete;
}

void STableDiffRevisionMenu::PopulateRevisionList(ECommandResult::Type InResult)
{
	if (MenuBox.IsValid() == false)
	{
		return;
	}

	MenuBox->ClearChildren();

	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
	const bool bUsesChangelists = Provider.UsesChangelists();
	const FText ProviderName = FText::FromName(Provider.GetName());

	MenuBox->AddSlot()
	.AutoHeight()
	.Padding(8.f, 6.f, 8.f, 2.f)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("HistoryHeader", "History ({0})"), ProviderName))
		.Font(FAppStyle::GetFontStyle("NormalFontBold"))
	];

	if (InResult != ECommandResult::Succeeded)
	{
		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("QueryFailed", "Could not read revision history. Check that revision control is connected."))
			.AutoWrapText(true)
		];
		return;
	}

	FSourceControlStatePtr SourceControlState = Provider.GetState(Filename, EStateCacheUsage::Use);
	if (SourceControlState.IsValid() == false || SourceControlState->IsSourceControlled() == false)
	{
		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NotControlled", "This Data Table is not under revision control."))
			.AutoWrapText(true)
		];
		return;
	}

	if (SourceControlState->GetHistorySize() <= 0)
	{
		MenuBox->AddSlot()
		.AutoHeight()
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoRevisions", "No revisions found."))
			.AutoWrapText(true)
		];
		return;
	}

	int32 LatestRevision = 0;
	for (int32 HistoryIndex = 0; HistoryIndex < SourceControlState->GetHistorySize(); ++HistoryIndex)
	{
		TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = SourceControlState->GetHistoryItem(HistoryIndex);
		if (Revision.IsValid() && Revision->GetRevisionNumber() > LatestRevision)
		{
			LatestRevision = Revision->GetRevisionNumber();
		}
	}

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);
	bool bLabeledHead = false;
	const FNumberFormattingOptions NoGrouping = FNumberFormattingOptions::DefaultNoGrouping();

	for (int32 HistoryIndex = 0; HistoryIndex < SourceControlState->GetHistorySize(); ++HistoryIndex)
	{
		TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = SourceControlState->GetHistoryItem(HistoryIndex);
		if (Revision.IsValid() == false)
		{
			continue;
		}

		const bool bIsHead = bLabeledHead == false && LatestRevision > 0 && Revision->GetRevisionNumber() == LatestRevision;
		if (bIsHead)
		{
			bLabeledHead = true;
		}

		FText Label;
		if (bIsHead && bUsesChangelists)
		{
			Label = LOCTEXT("Depot", "Depot");
		}
		else if (bIsHead)
		{
			Label = LOCTEXT("Head", "HEAD");
		}
		else if (bUsesChangelists)
		{
			Label = FText::Format(LOCTEXT("RevisionNumber", "Revision {0}"), FText::AsNumber(Revision->GetRevisionNumber(), &NoGrouping));
		}
		else
		{
			Label = FText::Format(LOCTEXT("RevisionId", "Revision {0}"), FText::FromString(Revision->GetRevision()));
		}

		FText Subtitle;
		if (bUsesChangelists)
		{
			Subtitle = FText::Format(
				LOCTEXT("ChangelistSubtitle", "CL {0}  {1}  {2}  {3}"),
				FText::AsNumber(Revision->GetCheckInIdentifier(), &NoGrouping),
				FText::FromString(Revision->GetUserName()),
				FText::AsDateTime(Revision->GetDate()),
				FText::FromString(Revision->GetAction()));
		}
		else
		{
			Subtitle = FText::Format(
				LOCTEXT("CommitSubtitle", "{0}  {1}  {2}  {3}"),
				FText::FromString(Revision->GetRevision()),
				FText::FromString(Revision->GetUserName()),
				FText::AsDateTime(Revision->GetDate()),
				FText::FromString(Revision->GetAction()));
		}

		FString Description = Revision->GetDescription();
		Description.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		int32 BreakAt = INDEX_NONE;
		if (Description.FindChar(TEXT('\n'), BreakAt))
		{
			Description = Description.Left(BreakAt);
		}
		Description.TrimStartAndEndInline();

		const FText Tooltip = FText::Format(
			LOCTEXT("RevisionTooltip", "{0}\n{1}\n{2}"),
			Label,
			Subtitle,
			FText::FromString(Revision->GetDescription()));

		TWeakObjectPtr<UDataTable> WeakTable = Table;
		ScrollBox->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "Menu.Button")
			.ContentPadding(FMargin(8.f, 4.f))
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([Revision, WeakTable]()
			{
				TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> RevisionCopy = Revision;
				TWeakObjectPtr<UDataTable> TableCopy = WeakTable;
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([RevisionCopy, TableCopy](float)
				{
					UDataTable* CurrentTable = TableCopy.Get();
					if (CurrentTable != nullptr && RevisionCopy.IsValid())
					{
						STableDiff::OpenAgainstRevision(CurrentTable, RevisionCopy.ToSharedRef());
					}
					return false;
				}));
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Label)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Subtitle)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Description))
					.Visibility(Description.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
				]
			]
		];
	}

	MenuBox->AddSlot()
	.AutoHeight()
	[
		SNew(SBox)
		.MaxDesiredHeight(520.f)
		[
			ScrollBox
		]
	];
}

#undef LOCTEXT_NAMESPACE
