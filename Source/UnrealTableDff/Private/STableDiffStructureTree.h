#pragma once

#include "TableDiffTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class ITableRow;
class STableViewBase;

DECLARE_DELEGATE_OneParam(FOnTableDiffStructureNodeSelected, TSharedPtr<FTableDiffPropertyNode>);

class STableDiffStructureTree : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STableDiffStructureTree) {}
		SLATE_ARGUMENT(const TArray<TSharedPtr<FTableDiffAlignedRow>>*, Rows)
		SLATE_EVENT(FOnTableDiffStructureNodeSelected, OnNodeSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Refresh(const FString& InFilterText, bool bInChangesOnly);
	void Reveal(FName RowName, const FString& FieldPath);
	TSharedRef<SWidget> MakeCell(TSharedPtr<FTableDiffPropertyNode> Node, FName ColumnId);
	FSlateColor GetCellBackground(TSharedPtr<FTableDiffPropertyNode> Node, FName ColumnId) const;

private:
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FTableDiffPropertyNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FTableDiffPropertyNode> Node, TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren) const;
	void OnSelectionChanged(TSharedPtr<FTableDiffPropertyNode> Node, ESelectInfo::Type SelectInfo);

	bool KeepNode(const TSharedPtr<FTableDiffPropertyNode>& Node, bool bWholeRowChange, TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren);
	void ExpandChangedNodes(const TSharedPtr<FTableDiffPropertyNode>& Node);
	TSharedPtr<FTableDiffPropertyNode> FindNode(const TSharedPtr<FTableDiffPropertyNode>& Node, FName RowName, const FString& FieldPath) const;
	bool ExpandToNode(const TSharedPtr<FTableDiffPropertyNode>& Node, const TSharedPtr<FTableDiffPropertyNode>& Target);
	void GetVisibleChildren(const TSharedPtr<FTableDiffPropertyNode>& Node, TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren) const;

	const TArray<TSharedPtr<FTableDiffAlignedRow>>* Rows = nullptr;
	FOnTableDiffStructureNodeSelected OnNodeSelected;
	TSharedPtr<STreeView<TSharedPtr<FTableDiffPropertyNode>>> TreeView;
	TArray<TSharedPtr<FTableDiffPropertyNode>> VisibleRoots;
	TMap<TSharedPtr<FTableDiffPropertyNode>, TArray<TSharedPtr<FTableDiffPropertyNode>>> FilteredChildren;
	FString FilterText;
	bool bChangesOnly = false;
	bool bUseFilter = false;
	bool bApplyingReveal = false;
	float NameColumnWidth = 280.f;
	float HistoryColumnWidth = 420.f;
	float CurrentColumnWidth = 420.f;

	float GetContentWidth() const;
};
