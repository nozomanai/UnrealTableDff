#include "STableDiff.h"

#include "STableDiffStructureTree.h"
#include "TableDiffModel.h"
#include "DiffUtils.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STableDiff"

DEFINE_LOG_CATEGORY_STATIC(LogTableDiff, Log, All);

namespace TableDiffPrivate
{
	UDataTable* FindDataTableInPackage(UPackage* Package, const FString& AssetName)
	{
		if (Package == nullptr)
		{
			return nullptr;
		}

		if (UDataTable* NamedTable = FindObject<UDataTable>(Package, *AssetName))
		{
			return NamedTable;
		}

		if (UDataTable* RootAsset = Cast<UDataTable>(Package->FindAssetInPackage()))
		{
			return RootAsset;
		}

		UDataTable* Found = nullptr;
		ForEachObjectWithPackage(Package, [&Found](UObject* Object)
		{
			if (Found == nullptr)
			{
				Found = Cast<UDataTable>(Object);
			}
			return true;
		});
		return Found;
	}

	FText DetailValueText(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return LOCTEXT("EmptyValue", "(empty)");
		}
		return FText::FromString(Value);
	}
}

class STableDiffRow : public SMultiColumnTableRow<TSharedPtr<FTableDiffAlignedRow>>
{
public:
	SLATE_BEGIN_ARGS(STableDiffRow) {}
		SLATE_ARGUMENT(TSharedPtr<FTableDiffAlignedRow>, Row)
		SLATE_ARGUMENT(ETableDiffSide, Side)
		SLATE_ARGUMENT(TWeakPtr<STableDiff>, DiffWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Row = InArgs._Row;
		Side = InArgs._Side;
		DiffWidget = InArgs._DiffWidget;
		SMultiColumnTableRow<TSharedPtr<FTableDiffAlignedRow>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (TSharedPtr<STableDiff> Diff = DiffWidget.Pin())
		{
			return Diff->MakeCellWidget(Row, Side, ColumnName);
		}
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FTableDiffAlignedRow> Row;
	ETableDiffSide Side = ETableDiffSide::Old;
	TWeakPtr<STableDiff> DiffWidget;
};

void STableDiff::OpenAgainstRevision(UDataTable* CurrentTable, const TSharedRef<ISourceControlRevision, ESPMode::ThreadSafe>& Revision)
{
	if (CurrentTable == nullptr)
	{
		return;
	}

	TSharedPtr<SWindow> DiffWindow;
	FText ErrorText;

	{
		// The slow-task dialog is itself a modal window. Parenting the diff window to it
		// destroys the diff window as soon as the progress dialog closes.
		FScopedSlowTask SlowTask(2.f, LOCTEXT("LoadingRevision", "Loading Data Table revision..."));
		SlowTask.MakeDialog();
		SlowTask.EnterProgressFrame(1.f);

		UPackage* LoadedPackage = DiffUtils::LoadPackageForDiff(Revision);
		if (LoadedPackage == nullptr)
		{
			UE_LOG(LogTableDiff, Warning, TEXT("TableDiff could not load revision %s of %s"), *Revision->GetRevision(), *CurrentTable->GetPathName());
			ErrorText = LOCTEXT("LoadFailed", "Unable to load that revision. The content may no longer be supported by this engine.");
		}
		else
		{
			TStrongObjectPtr<UPackage> PackagePin(LoadedPackage);
			UDataTable* OldTable = TableDiffPrivate::FindDataTableInPackage(LoadedPackage, CurrentTable->GetName());
			if (OldTable == nullptr)
			{
				ErrorText = LOCTEXT("MissingTable", "That revision does not contain a Data Table.");
			}
			else
			{
				TStrongObjectPtr<UDataTable> OldPin(OldTable);
				TStrongObjectPtr<UDataTable> NewPin(CurrentTable);

				FRevisionInfo OldRevisionInfo;
				OldRevisionInfo.Revision = Revision->GetRevision();
				OldRevisionInfo.Changelist = Revision->GetCheckInIdentifier();
				OldRevisionInfo.Date = Revision->GetDate();

				FRevisionInfo LocalRevision;
				LocalRevision.Changelist = -1;

				SlowTask.EnterProgressFrame(1.f, LOCTEXT("BuildingDiff", "Comparing rows..."));

				const FText WindowTitle = FText::Format(LOCTEXT("WindowTitle", "{0} - TableDiff"), FText::FromString(CurrentTable->GetName()));
				TSharedRef<SWindow> Window = SNew(SWindow)
					.Title(WindowTitle)
					.ClientSize(FVector2D(1500.f, 900.f))
					.SizingRule(ESizingRule::UserSized)
					.SupportsMaximize(true)
					.SupportsMinimize(true);

				Window->SetContent(
					SNew(STableDiff)
					.OldTable(OldTable)
					.NewTable(CurrentTable)
					.OldPackage(LoadedPackage)
					.OldRevision(OldRevisionInfo)
					.NewRevision(LocalRevision));
				DiffWindow = Window;
			}
		}
	}

	if (ErrorText.IsEmpty() == false)
	{
		FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
		return;
	}

	if (DiffWindow.IsValid())
	{
		FSlateApplication::Get().AddWindow(DiffWindow.ToSharedRef());
	}
}

void STableDiff::Construct(const FArguments& InArgs)
{
	OldTablePtr.Reset(InArgs._OldTable);
	NewTablePtr.Reset(InArgs._NewTable);
	OldPackagePtr.Reset(InArgs._OldPackage);
	OldRevision = InArgs._OldRevision;
	NewRevision = InArgs._NewRevision;

	Result = FTableDiffModel::Build(OldTablePtr.Get(), NewTablePtr.Get());
	VisibleRows = Result.Rows;
	VisibleDiffs = Result.DiffRoots;

	const FText SummaryText = FText::Format(
		LOCTEXT("Summary", "{0} added, {1} removed, {2} modified"),
		FText::AsNumber(Result.AddedCount),
		FText::AsNumber(Result.RemovedCount),
		FText::AsNumber(Result.ModifiedCount));
	const FText OldTitle = DiffViewUtils::GetPanelLabel(OldTablePtr.Get(), OldRevision, FText::GetEmpty());
	const FText NewTitle = DiffViewUtils::GetPanelLabel(NewTablePtr.Get(), NewRevision, FText::GetEmpty());

	FToolBarBuilder NavToolBar(TSharedPtr<FUICommandList>(), FMultiBoxCustomization::None);
	NavToolBar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &STableDiff::PrevDiff),
			FCanExecuteAction::CreateSP(this, &STableDiff::HasPrevDiff)),
		NAME_None,
		LOCTEXT("PrevDiff", "Prev"),
		LOCTEXT("PrevDiffTip", "Go to the previous difference"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintDif.PrevDiff"));
	NavToolBar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &STableDiff::NextDiff),
			FCanExecuteAction::CreateSP(this, &STableDiff::HasNextDiff)),
		NAME_None,
		LOCTEXT("NextDiff", "Next"),
		LOCTEXT("NextDiffTip", "Go to the next difference"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintDif.NextDiff"));
	NavToolBar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &STableDiff::ToggleLockScroll),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STableDiff::IsLockScrollChecked)),
		NAME_None,
		LOCTEXT("LockScroll", "Lock Scroll"),
		LOCTEXT("LockScrollTip", "Scroll the history and current tables together"),
		FSlateIcon(),
		EUserInterfaceActionType::ToggleButton);
	NavToolBar.AddSeparator();
	NavToolBar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &STableDiff::ShowGridView),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STableDiff::IsGridView)),
		NAME_None,
		LOCTEXT("GridView", "Grid"),
		LOCTEXT("GridViewTip", "Show both revisions as side-by-side data table grids"),
		FSlateIcon(),
		EUserInterfaceActionType::ToggleButton);
	NavToolBar.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &STableDiff::ShowStructureView),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &STableDiff::IsStructureView)),
		NAME_None,
		LOCTEXT("StructureView", "Structure"),
		LOCTEXT("StructureViewTip", "Show each row struct as an expandable tree and highlight differences"),
		FSlateIcon(),
		EUserInterfaceActionType::ToggleButton);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Docking.Tab", ".ContentAreaBrush"))
		.Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f, 4.f, 4.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					NavToolBar.MakeWidget()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12.f, 0.f, 4.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &STableDiff::GetChangesOnlyState)
					.OnCheckStateChanged(this, &STableDiff::OnChangesOnlyChanged)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ChangesOnly", "Changes only"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f)
				[
					SNew(STextBlock)
					.Text(SummaryText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 4.f, 0.f)
				[
					SNew(SBox)
					.MinDesiredWidth(220.f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("FilterHint", "Filter rows"))
						.OnTextChanged(this, &STableDiff::OnFilterTextChanged)
					]
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(4.f, 0.f)
			[
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(0.26f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.f, 6.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Differences", "Differences"))
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.f)
						[
							SAssignNew(DiffTree, STreeView<TSharedPtr<FTableDiffEntry>>)
							.TreeItemsSource(&VisibleDiffs)
							.OnGenerateRow(this, &STableDiff::OnGenerateDiffRow)
							.OnGetChildren(this, &STableDiff::OnGetDiffChildren)
							.OnSelectionChanged(this, &STableDiff::OnDiffSelectionChanged)
							.SelectionMode(ESelectionMode::Single)
						]
					]
				]
				+ SSplitter::Slot()
				.Value(0.74f)
				[
					SAssignNew(ViewSwitcher, SWidgetSwitcher)
					+ SWidgetSwitcher::Slot()
					[
						SNew(SSplitter)
						+ SSplitter::Slot()
						[
							MakeTablePanel(ETableDiffSide::Old, OldTitle)
						]
						+ SSplitter::Slot()
						[
							MakeTablePanel(ETableDiffSide::New, NewTitle)
						]
					]
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(StructureTree, STableDiffStructureTree)
						.Rows(&Result.Rows)
						.OnNodeSelected(this, &STableDiff::OnStructureNodeSelected)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(8.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 0.f, 0.f, 6.f)
					[
						SAssignNew(SelectedChangeLabel, STextBlock)
						.Text(LOCTEXT("SelectChange", "Select a change"))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(130.f)
						[
							SNew(SSplitter)
							+ SSplitter::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("HistoryValue", "History"))
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								]
								+ SVerticalBox::Slot()
								.FillHeight(1.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(OldDetailText, SMultiLineEditableText)
										.IsReadOnly(true)
										.AutoWrapText(true)
										.TextStyle(FAppStyle::Get(), "NormalText")
									]
								]
							]
							+ SSplitter::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("CurrentValue", "Current"))
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								]
								+ SVerticalBox::Slot()
								.FillHeight(1.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(NewDetailText, SMultiLineEditableText)
										.IsReadOnly(true)
										.AutoWrapText(true)
										.TextStyle(FAppStyle::Get(), "NormalText")
									]
								]
							]
						]
					]
				]
			]
		]
	];

	if (DiffTree.IsValid())
	{
		for (const TSharedPtr<FTableDiffEntry>& Entry : VisibleDiffs)
		{
			DiffTree->SetItemExpansion(Entry, true);
		}
	}
	if (StructureTree.IsValid())
	{
		StructureTree->Refresh(FilterString, bChangesOnly);
	}
}

void STableDiff::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bLockScroll == false || OldListView.IsValid() == false || NewListView.IsValid() == false)
	{
		return;
	}

	const float OldOffset = OldListView->GetScrollOffset();
	const float NewOffset = NewListView->GetScrollOffset();
	const bool bOldChanged = FMath::IsNearlyEqual(OldOffset, LastOldScrollOffset) == false;
	const bool bNewChanged = FMath::IsNearlyEqual(NewOffset, LastNewScrollOffset) == false;
	if (bOldChanged && bNewChanged == false)
	{
		NewListView->SetScrollOffset(OldOffset);
	}
	else if (bNewChanged && bOldChanged == false)
	{
		OldListView->SetScrollOffset(NewOffset);
	}

	LastOldScrollOffset = OldListView->GetScrollOffset();
	LastNewScrollOffset = NewListView->GetScrollOffset();
}

TSharedRef<SWidget> STableDiff::MakeCellWidget(TSharedPtr<FTableDiffAlignedRow> Row, ETableDiffSide Side, FName HeaderId)
{
	if (Row.IsValid() == false)
	{
		return SNullWidget::NullWidget;
	}

	FText CellText;
	FText Tooltip;
	if (HeaderId == TableDiffColumns::RowNumber)
	{
		const int32 Index = Side == ETableDiffSide::Old ? Row->OldIndex : Row->NewIndex;
		if (Index != INDEX_NONE)
		{
			CellText = FText::AsNumber(Index + 1);
		}
		Tooltip = CellText;
	}
	else if (HeaderId == TableDiffColumns::RowName)
	{
		CellText = FText::FromName(Row->RowName);
		Tooltip = CellText;
	}
	else if (const FTableDiffColumn* Column = FindColumnByHeader(HeaderId))
	{
		if (const FTableDiffCell* Cell = Row->Cells.Find(Column->ColumnId))
		{
			CellText = Side == ETableDiffSide::Old ? Cell->OldText : Cell->NewText;
			if (Cell->bDiffers)
			{
				Tooltip = FText::Format(LOCTEXT("CellTip", "Old: {0}\nNew: {1}"), Cell->OldText, Cell->NewText);
			}
			else
			{
				Tooltip = CellText;
			}
		}
	}

	TWeakPtr<STableDiff> WeakThis = SharedThis(this);
	const TSharedPtr<FTableDiffAlignedRow> RowPtr = Row;
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([WeakThis, RowPtr, HeaderId, Side]()
		{
			if (TSharedPtr<STableDiff> Diff = WeakThis.Pin())
			{
				return Diff->GetCellBackground(RowPtr, HeaderId, Side);
			}
			return FSlateColor(FLinearColor::Transparent);
		})
		.Padding(FMargin(6.f, 2.f))
		[
			SNew(STextBlock)
			.Text(CellText)
			.ToolTipText(Tooltip)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		];
}

FSlateColor STableDiff::GetCellBackground(TSharedPtr<FTableDiffAlignedRow> Row, FName HeaderId, ETableDiffSide Side) const
{
	if (Row.IsValid() == false)
	{
		return FSlateColor(FLinearColor::Transparent);
	}

	const bool bRowFocused = FocusedRowName == Row->RowName;
	const FTableDiffColumn* Column = FindColumnByHeader(HeaderId);
	const bool bCellFocused = bRowFocused && Column != nullptr && FocusedColumnId == Column->ColumnId;
	const bool bWholeRowFocused = bRowFocused && FocusedColumnId.IsNone();
	const FLinearColor FocusBoost = (bCellFocused || bWholeRowFocused) ? FLinearColor(0.08f, 0.08f, 0.08f, 0.f) : FLinearColor::Transparent;

	if (Row->Kind == ETableDiffRowKind::Added)
	{
		if (Side == ETableDiffSide::New)
		{
			return FSlateColor(FLinearColor(0.05f, 0.22f, 0.08f) + FocusBoost);
		}
		return FSlateColor(FLinearColor(0.03f, 0.03f, 0.03f, 1.f));
	}
	if (Row->Kind == ETableDiffRowKind::Removed)
	{
		if (Side == ETableDiffSide::Old)
		{
			return FSlateColor(FLinearColor(0.26f, 0.06f, 0.06f) + FocusBoost);
		}
		return FSlateColor(FLinearColor(0.03f, 0.03f, 0.03f, 1.f));
	}

	if (Column != nullptr)
	{
		if (const FTableDiffCell* Cell = Row->Cells.Find(Column->ColumnId))
		{
			if (Cell->bDiffers)
			{
				return FSlateColor(FLinearColor(0.28f, 0.20f, 0.04f) + FocusBoost);
			}
		}
	}

	if (bRowFocused && (HeaderId == TableDiffColumns::RowName || HeaderId == TableDiffColumns::RowNumber))
	{
		return FSlateColor(FLinearColor(0.08f, 0.16f, 0.28f));
	}

	return FSlateColor(FLinearColor::Transparent);
}

TSharedRef<SHeaderRow> STableDiff::MakeHeaderRow()
{
	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow);
	Header->AddColumn(
		SHeaderRow::Column(TableDiffColumns::RowNumber)
		.DefaultLabel(LOCTEXT("RowNumber", "#"))
		.FixedWidth(48.f));
	Header->AddColumn(
		SHeaderRow::Column(TableDiffColumns::RowName)
		.DefaultLabel(LOCTEXT("RowName", "Row Name"))
		.ManualWidth(TAttribute<float>::CreateLambda([this]()
		{
			return GetRowNameColumnWidth();
		}))
		.OnWidthChanged(FOnWidthChanged::CreateLambda([this](float NewWidth)
		{
			OnRowNameColumnResized(NewWidth);
		})));

	for (int32 ColumnIndex = 0; ColumnIndex < Result.Columns.Num(); ++ColumnIndex)
	{
		const FTableDiffColumn& Column = Result.Columns[ColumnIndex];
		Header->AddColumn(
			SHeaderRow::Column(Column.HeaderId)
			.DefaultLabel(Column.DisplayName)
			.ManualWidth(TAttribute<float>::CreateLambda([this, ColumnIndex]()
			{
				return GetPropertyColumnWidth(ColumnIndex);
			}))
			.OnWidthChanged(FOnWidthChanged::CreateLambda([this, ColumnIndex](float NewWidth)
			{
				OnPropertyColumnResized(NewWidth, ColumnIndex);
			})));
	}
	return Header;
}

TSharedRef<SWidget> STableDiff::MakeTablePanel(ETableDiffSide Side, const FText& Title)
{
	if (Side == ETableDiffSide::Old)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.f, 6.f, 8.f, 4.f)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(0.f)
				[
					SAssignNew(OldListView, SListView<TSharedPtr<FTableDiffAlignedRow>>)
					.ListItemsSource(&VisibleRows)
					.OnGenerateRow(this, &STableDiff::OnGenerateOldRow)
					.OnSelectionChanged(this, &STableDiff::OnOldSelectionChanged)
					.HeaderRow(MakeHeaderRow())
					.SelectionMode(ESelectionMode::Single)
					.AllowOverscroll(EAllowOverscroll::No)
				]
			];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 6.f, 8.f, 4.f)
		[
			SNew(STextBlock)
			.Text(Title)
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.f)
			[
				SAssignNew(NewListView, SListView<TSharedPtr<FTableDiffAlignedRow>>)
				.ListItemsSource(&VisibleRows)
				.OnGenerateRow(this, &STableDiff::OnGenerateNewRow)
				.OnSelectionChanged(this, &STableDiff::OnNewSelectionChanged)
				.HeaderRow(MakeHeaderRow())
				.SelectionMode(ESelectionMode::Single)
				.AllowOverscroll(EAllowOverscroll::No)
			]
		];
}

TSharedRef<ITableRow> STableDiff::MakeRowWidget(TSharedPtr<FTableDiffAlignedRow> Row, const TSharedRef<STableViewBase>& OwnerTable, ETableDiffSide Side)
{
	return SNew(STableDiffRow, OwnerTable)
		.Row(Row)
		.Side(Side)
		.DiffWidget(SharedThis(this));
}

TSharedRef<ITableRow> STableDiff::OnGenerateOldRow(TSharedPtr<FTableDiffAlignedRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return MakeRowWidget(Row, OwnerTable, ETableDiffSide::Old);
}

TSharedRef<ITableRow> STableDiff::OnGenerateNewRow(TSharedPtr<FTableDiffAlignedRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return MakeRowWidget(Row, OwnerTable, ETableDiffSide::New);
}

TSharedRef<ITableRow> STableDiff::OnGenerateDiffRow(TSharedPtr<FTableDiffEntry> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FText Label = Item.IsValid() ? Item->Label : FText::GetEmpty();
	const FText Tooltip = Item.IsValid() ? Item->Tooltip : FText::GetEmpty();
	const FLinearColor Color = Item.IsValid() ? Item->Color : FLinearColor::White;
	return SNew(STableRow<TSharedPtr<FTableDiffEntry>>, OwnerTable)
		[
			SNew(SBox)
			.Padding(FMargin(4.f, 2.f))
			[
				SNew(STextBlock)
				.Text(Label)
				.ToolTipText(Tooltip)
				.ColorAndOpacity(Color)
			]
		];
}

void STableDiff::OnGetDiffChildren(TSharedPtr<FTableDiffEntry> Item, TArray<TSharedPtr<FTableDiffEntry>>& OutChildren) const
{
	if (Item.IsValid())
	{
		OutChildren.Append(Item->Children);
	}
}

void STableDiff::OnDiffSelectionChanged(TSharedPtr<FTableDiffEntry> Item, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingSelection || Item.IsValid() == false)
	{
		return;
	}

	bUpdatingSelection = true;
	FocusedRowName = Item->RowName;
	FocusedColumnId = Item->ColumnId;
	FocusedFieldPath = Item->FieldPath;
	SetDetailText(Item->Label, Item->OldValue, Item->NewValue);
	ScrollTablesToRow(Item->RowName);
	if (StructureTree.IsValid())
	{
		StructureTree->Reveal(Item->RowName, Item->FieldPath);
	}
	bUpdatingSelection = false;
}

void STableDiff::OnOldSelectionChanged(TSharedPtr<FTableDiffAlignedRow> Row, ESelectInfo::Type SelectInfo)
{
	OnTableSelectionChanged(Row, SelectInfo, ETableDiffSide::Old);
}

void STableDiff::OnNewSelectionChanged(TSharedPtr<FTableDiffAlignedRow> Row, ESelectInfo::Type SelectInfo)
{
	OnTableSelectionChanged(Row, SelectInfo, ETableDiffSide::New);
}

void STableDiff::OnTableSelectionChanged(TSharedPtr<FTableDiffAlignedRow> Row, ESelectInfo::Type SelectInfo, ETableDiffSide Side)
{
	if (bUpdatingSelection || SelectInfo == ESelectInfo::Direct || Row.IsValid() == false)
	{
		return;
	}

	bUpdatingSelection = true;
	if (Side == ETableDiffSide::Old && NewListView.IsValid())
	{
		NewListView->SetSelection(Row, ESelectInfo::Direct);
	}
	else if (Side == ETableDiffSide::New && OldListView.IsValid())
	{
		OldListView->SetSelection(Row, ESelectInfo::Direct);
	}
	FocusedRowName = Row->RowName;
	FocusedColumnId = NAME_None;
	bUpdatingSelection = false;
	SelectDiffForRow(Row->RowName);
}

void STableDiff::OnFilterTextChanged(const FText& NewText)
{
	FilterString = NewText.ToString();
	RefreshFilteredViews();
}

void STableDiff::OnChangesOnlyChanged(ECheckBoxState NewState)
{
	bChangesOnly = NewState == ECheckBoxState::Checked;
	RefreshFilteredViews();
}

ECheckBoxState STableDiff::GetChangesOnlyState() const
{
	if (bChangesOnly)
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

void STableDiff::RefreshFilteredViews()
{
	VisibleRows.Reset();
	for (const TSharedPtr<FTableDiffAlignedRow>& Row : Result.Rows)
	{
		if (PassesRowFilter(Row))
		{
			VisibleRows.Add(Row);
		}
	}

	VisibleDiffs.Reset();
	for (const TSharedPtr<FTableDiffEntry>& Entry : Result.DiffRoots)
	{
		if (EntryMatches(Entry))
		{
			VisibleDiffs.Add(Entry);
		}
	}

	if (OldListView.IsValid())
	{
		OldListView->RequestListRefresh();
	}
	if (NewListView.IsValid())
	{
		NewListView->RequestListRefresh();
	}
	if (DiffTree.IsValid())
	{
		DiffTree->RequestTreeRefresh();
		for (const TSharedPtr<FTableDiffEntry>& Entry : VisibleDiffs)
		{
			DiffTree->SetItemExpansion(Entry, true);
		}
	}
	if (StructureTree.IsValid())
	{
		StructureTree->Refresh(FilterString, bChangesOnly);
		if (FocusedRowName.IsNone() == false || FocusedFieldPath.IsEmpty() == false)
		{
			StructureTree->Reveal(FocusedRowName, FocusedFieldPath);
		}
	}
}

bool STableDiff::PassesRowFilter(const TSharedPtr<FTableDiffAlignedRow>& Row) const
{
	if (Row.IsValid() == false)
	{
		return false;
	}
	if (bChangesOnly && Row->Kind == ETableDiffRowKind::Unchanged)
	{
		return false;
	}
	if (FilterString.IsEmpty())
	{
		return true;
	}
	if (Row->RowName.ToString().Contains(FilterString, ESearchCase::IgnoreCase))
	{
		return true;
	}
	for (const TPair<FName, FTableDiffCell>& Pair : Row->Cells)
	{
		if (Pair.Value.OldText.ToString().Contains(FilterString, ESearchCase::IgnoreCase)
			|| Pair.Value.NewText.ToString().Contains(FilterString, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool STableDiff::EntryMatches(const TSharedPtr<FTableDiffEntry>& Entry) const
{
	if (Entry.IsValid() == false)
	{
		return false;
	}
	if (FilterString.IsEmpty())
	{
		return true;
	}
	if (Entry->Label.ToString().Contains(FilterString, ESearchCase::IgnoreCase)
		|| Entry->RowName.ToString().Contains(FilterString, ESearchCase::IgnoreCase)
		|| Entry->OldValue.Contains(FilterString, ESearchCase::IgnoreCase)
		|| Entry->NewValue.Contains(FilterString, ESearchCase::IgnoreCase))
	{
		return true;
	}
	for (const TSharedPtr<FTableDiffEntry>& Child : Entry->Children)
	{
		if (EntryMatches(Child))
		{
			return true;
		}
	}
	return false;
}

void STableDiff::ShowGridView()
{
	ViewMode = ETableDiffViewMode::Grid;
	if (ViewSwitcher.IsValid())
	{
		ViewSwitcher->SetActiveWidgetIndex(0);
	}
}

void STableDiff::ShowStructureView()
{
	ViewMode = ETableDiffViewMode::Structure;
	if (ViewSwitcher.IsValid())
	{
		ViewSwitcher->SetActiveWidgetIndex(1);
	}
	if (StructureTree.IsValid())
	{
		StructureTree->Refresh(FilterString, bChangesOnly);
		if (FocusedRowName.IsNone() == false || FocusedFieldPath.IsEmpty() == false)
		{
			StructureTree->Reveal(FocusedRowName, FocusedFieldPath);
		}
	}
}

bool STableDiff::IsGridView() const
{
	return ViewMode == ETableDiffViewMode::Grid;
}

bool STableDiff::IsStructureView() const
{
	return ViewMode == ETableDiffViewMode::Structure;
}

void STableDiff::OnStructureNodeSelected(TSharedPtr<FTableDiffPropertyNode> Node)
{
	if (bUpdatingSelection || Node.IsValid() == false)
	{
		return;
	}

	bUpdatingSelection = true;
	FocusedRowName = Node->RowName;
	FocusedFieldPath = Node->Path;
	FocusedColumnId = NAME_None;
	const FText Label = Node->bIsRow
		? FText::Format(LOCTEXT("StructureRow", "Row {0}"), Node->DisplayName)
		: Node->DisplayName;
	SetDetailText(Label, Node->OldValue, Node->NewValue);
	SelectDiffForField(Node->RowName, Node->Path);
	ScrollTablesToRow(Node->RowName);
	bUpdatingSelection = false;
}

void STableDiff::SelectDiffForField(FName RowName, const FString& FieldPath)
{
	if (DiffTree.IsValid() == false)
	{
		return;
	}

	TArray<TSharedPtr<FTableDiffEntry>> Navigable;
	CollectNavigable(VisibleDiffs, Navigable);
	TSharedPtr<FTableDiffEntry> RowFallback;
	for (const TSharedPtr<FTableDiffEntry>& Entry : Navigable)
	{
		if (Entry.IsValid() == false || Entry->RowName != RowName)
		{
			continue;
		}
		if (Entry->FieldPath == FieldPath)
		{
			DiffTree->SetSelection(Entry, ESelectInfo::Direct);
			DiffTree->RequestScrollIntoView(Entry);
			return;
		}
		if (RowFallback.IsValid() == false && Entry->FieldPath.IsEmpty())
		{
			RowFallback = Entry;
		}
	}

	if (FieldPath.IsEmpty() && RowFallback.IsValid())
	{
		DiffTree->SetSelection(RowFallback, ESelectInfo::Direct);
		DiffTree->RequestScrollIntoView(RowFallback);
		return;
	}

	DiffTree->ClearSelection();
}

void STableDiff::PrevDiff()
{
	StepDiff(-1);
}

void STableDiff::NextDiff()
{
	StepDiff(1);
}

bool STableDiff::HasPrevDiff() const
{
	TArray<TSharedPtr<FTableDiffEntry>> Navigable;
	CollectNavigable(VisibleDiffs, Navigable);
	if (Navigable.Num() == 0 || DiffTree.IsValid() == false)
	{
		return false;
	}
	const TArray<TSharedPtr<FTableDiffEntry>> Selected = DiffTree->GetSelectedItems();
	if (Selected.Num() == 0)
	{
		return false;
	}
	const int32 Index = Navigable.IndexOfByKey(Selected[0]);
	return Index > 0;
}

bool STableDiff::HasNextDiff() const
{
	TArray<TSharedPtr<FTableDiffEntry>> Navigable;
	CollectNavigable(VisibleDiffs, Navigable);
	if (Navigable.Num() == 0 || DiffTree.IsValid() == false)
	{
		return false;
	}
	const TArray<TSharedPtr<FTableDiffEntry>> Selected = DiffTree->GetSelectedItems();
	if (Selected.Num() == 0)
	{
		return true;
	}
	const int32 Index = Navigable.IndexOfByKey(Selected[0]);
	if (Index == INDEX_NONE)
	{
		return true;
	}
	return Index + 1 < Navigable.Num();
}

void STableDiff::StepDiff(int32 Direction)
{
	TArray<TSharedPtr<FTableDiffEntry>> Navigable;
	CollectNavigable(VisibleDiffs, Navigable);
	if (Navigable.Num() == 0 || DiffTree.IsValid() == false)
	{
		return;
	}

	const TArray<TSharedPtr<FTableDiffEntry>> Selected = DiffTree->GetSelectedItems();
	int32 Index = INDEX_NONE;
	if (Selected.Num() > 0)
	{
		Index = Navigable.IndexOfByKey(Selected[0]);
	}

	int32 NextIndex = INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		NextIndex = Direction > 0 ? 0 : Navigable.Num() - 1;
	}
	else
	{
		NextIndex = Index + Direction;
	}
	if (Navigable.IsValidIndex(NextIndex) == false)
	{
		return;
	}

	DiffTree->SetSelection(Navigable[NextIndex], ESelectInfo::Direct);
	DiffTree->RequestScrollIntoView(Navigable[NextIndex]);
}

void STableDiff::CollectNavigable(const TArray<TSharedPtr<FTableDiffEntry>>& Nodes, TArray<TSharedPtr<FTableDiffEntry>>& OutNavigable) const
{
	for (const TSharedPtr<FTableDiffEntry>& Node : Nodes)
	{
		if (Node.IsValid() == false)
		{
			continue;
		}
		if (Node->bNavigable)
		{
			OutNavigable.Add(Node);
		}
		CollectNavigable(Node->Children, OutNavigable);
	}
}

void STableDiff::ScrollTablesToRow(FName RowName)
{
	if (RowName.IsNone())
	{
		return;
	}

	for (const TSharedPtr<FTableDiffAlignedRow>& Row : VisibleRows)
	{
		if (Row.IsValid() && Row->RowName == RowName)
		{
			if (OldListView.IsValid())
			{
				OldListView->RequestScrollIntoView(Row);
				OldListView->SetSelection(Row, ESelectInfo::Direct);
			}
			if (NewListView.IsValid())
			{
				NewListView->RequestScrollIntoView(Row);
				NewListView->SetSelection(Row, ESelectInfo::Direct);
			}
			return;
		}
	}
}

void STableDiff::SelectDiffForRow(FName RowName)
{
	if (DiffTree.IsValid() == false)
	{
		return;
	}

	TArray<TSharedPtr<FTableDiffEntry>> Navigable;
	CollectNavigable(VisibleDiffs, Navigable);
	for (const TSharedPtr<FTableDiffEntry>& Entry : Navigable)
	{
		if (Entry.IsValid() && Entry->RowName == RowName)
		{
			DiffTree->SetSelection(Entry, ESelectInfo::OnMouseClick);
			DiffTree->RequestScrollIntoView(Entry);
			return;
		}
	}

	DiffTree->ClearSelection();
	SetDetailText(FText::FromName(RowName), FString(), FString());
}

void STableDiff::SetDetailText(const FText& Label, const FString& OldValue, const FString& NewValue)
{
	if (SelectedChangeLabel.IsValid())
	{
		SelectedChangeLabel->SetText(Label);
	}
	if (OldDetailText.IsValid())
	{
		OldDetailText->SetText(TableDiffPrivate::DetailValueText(OldValue));
	}
	if (NewDetailText.IsValid())
	{
		NewDetailText->SetText(TableDiffPrivate::DetailValueText(NewValue));
	}
}

void STableDiff::ToggleLockScroll()
{
	bLockScroll = bLockScroll == false;
}

bool STableDiff::IsLockScrollChecked() const
{
	return bLockScroll;
}

float STableDiff::GetRowNameColumnWidth() const
{
	return Result.RowNameWidth;
}

float STableDiff::GetPropertyColumnWidth(int32 ColumnIndex) const
{
	if (Result.Columns.IsValidIndex(ColumnIndex))
	{
		return Result.Columns[ColumnIndex].Width;
	}
	return 120.f;
}

void STableDiff::OnRowNameColumnResized(float NewWidth)
{
	Result.RowNameWidth = FMath::Max(NewWidth, 80.f);
}

void STableDiff::OnPropertyColumnResized(float NewWidth, int32 ColumnIndex)
{
	if (Result.Columns.IsValidIndex(ColumnIndex))
	{
		Result.Columns[ColumnIndex].Width = FMath::Max(NewWidth, 48.f);
	}
}

const FTableDiffColumn* STableDiff::FindColumnByHeader(FName HeaderId) const
{
	for (const FTableDiffColumn& Column : Result.Columns)
	{
		if (Column.HeaderId == HeaderId)
		{
			return &Column;
		}
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
