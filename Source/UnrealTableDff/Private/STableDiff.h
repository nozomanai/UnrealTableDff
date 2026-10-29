#pragma once

#include "TableDiffTypes.h"
#include "AssetDefinition.h"
#include "ISourceControlRevision.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

class ITableRow;
class SHeaderRow;
class SMultiLineEditableText;
class STableViewBase;
class STextBlock;
class UDataTable;
class UPackage;

class STableDiff : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STableDiff) {}
		SLATE_ARGUMENT(UDataTable*, OldTable)
		SLATE_ARGUMENT(UDataTable*, NewTable)
		SLATE_ARGUMENT(UPackage*, OldPackage)
		SLATE_ARGUMENT(FRevisionInfo, OldRevision)
		SLATE_ARGUMENT(FRevisionInfo, NewRevision)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	static void OpenAgainstRevision(UDataTable* CurrentTable, const TSharedRef<ISourceControlRevision, ESPMode::ThreadSafe>& Revision);

	TSharedRef<SWidget> MakeCellWidget(TSharedPtr<FTableDiffAlignedRow> Row, ETableDiffSide Side, FName HeaderId);
	FSlateColor GetCellBackground(TSharedPtr<FTableDiffAlignedRow> Row, FName HeaderId, ETableDiffSide Side) const;

private:
	TSharedRef<SHeaderRow> MakeHeaderRow();
	TSharedRef<SWidget> MakeTablePanel(ETableDiffSide Side, const FText& Title);
	TSharedRef<ITableRow> MakeRowWidget(TSharedPtr<FTableDiffAlignedRow> Row, const TSharedRef<STableViewBase>& OwnerTable, ETableDiffSide Side);
	TSharedRef<ITableRow> OnGenerateOldRow(TSharedPtr<FTableDiffAlignedRow> Row, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateNewRow(TSharedPtr<FTableDiffAlignedRow> Row, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateDiffRow(TSharedPtr<FTableDiffEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);

	void OnGetDiffChildren(TSharedPtr<FTableDiffEntry> Item, TArray<TSharedPtr<FTableDiffEntry>>& OutChildren) const;
	void OnDiffSelectionChanged(TSharedPtr<FTableDiffEntry> Item, ESelectInfo::Type SelectInfo);
	void OnOldSelectionChanged(TSharedPtr<FTableDiffAlignedRow> Row, ESelectInfo::Type SelectInfo);
	void OnNewSelectionChanged(TSharedPtr<FTableDiffAlignedRow> Row, ESelectInfo::Type SelectInfo);
	void OnTableSelectionChanged(TSharedPtr<FTableDiffAlignedRow> Row, ESelectInfo::Type SelectInfo, ETableDiffSide Side);

	void OnFilterTextChanged(const FText& NewText);
	void OnChangesOnlyChanged(ECheckBoxState NewState);
	ECheckBoxState GetChangesOnlyState() const;
	void RefreshFilteredViews();
	bool PassesRowFilter(const TSharedPtr<FTableDiffAlignedRow>& Row) const;
	bool EntryMatches(const TSharedPtr<FTableDiffEntry>& Entry) const;

	void PrevDiff();
	void NextDiff();
	bool HasPrevDiff() const;
	bool HasNextDiff() const;
	void StepDiff(int32 Direction);
	void CollectNavigable(const TArray<TSharedPtr<FTableDiffEntry>>& Nodes, TArray<TSharedPtr<FTableDiffEntry>>& OutNavigable) const;
	void ScrollTablesToRow(FName RowName);
	void SelectDiffForRow(FName RowName);
	void SetDetailText(const FText& Label, const FString& OldValue, const FString& NewValue);

	void ToggleLockScroll();
	bool IsLockScrollChecked() const;
	float GetRowNameColumnWidth() const;
	float GetPropertyColumnWidth(int32 ColumnIndex) const;
	void OnRowNameColumnResized(float NewWidth);
	void OnPropertyColumnResized(float NewWidth, int32 ColumnIndex);
	const FTableDiffColumn* FindColumnByHeader(FName HeaderId) const;

	FTableDiffResult Result;
	TArray<TSharedPtr<FTableDiffAlignedRow>> VisibleRows;
	TArray<TSharedPtr<FTableDiffEntry>> VisibleDiffs;
	FRevisionInfo OldRevision;
	FRevisionInfo NewRevision;
	FString FilterString;
	FName FocusedRowName;
	FName FocusedColumnId;
	bool bChangesOnly = false;
	bool bLockScroll = true;
	bool bUpdatingSelection = false;
	float LastOldScrollOffset = 0.f;
	float LastNewScrollOffset = 0.f;

	TStrongObjectPtr<UDataTable> OldTablePtr;
	TStrongObjectPtr<UDataTable> NewTablePtr;
	TStrongObjectPtr<UPackage> OldPackagePtr;

	TSharedPtr<SListView<TSharedPtr<FTableDiffAlignedRow>>> OldListView;
	TSharedPtr<SListView<TSharedPtr<FTableDiffAlignedRow>>> NewListView;
	TSharedPtr<STreeView<TSharedPtr<FTableDiffEntry>>> DiffTree;
	TSharedPtr<STextBlock> SelectedChangeLabel;
	TSharedPtr<SMultiLineEditableText> OldDetailText;
	TSharedPtr<SMultiLineEditableText> NewDetailText;
};
