#include "STableDiffStructureTree.h"

#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STableDiffStructureTree"

namespace TableDiffStructureColumns
{
	const FName Name(TEXT("StructureName"));
	const FName History(TEXT("StructureHistory"));
	const FName Current(TEXT("StructureCurrent"));
}

class STableDiffStructureRow : public SMultiColumnTableRow<TSharedPtr<FTableDiffPropertyNode>>
{
public:
	SLATE_BEGIN_ARGS(STableDiffStructureRow) {}
		SLATE_ARGUMENT(TSharedPtr<FTableDiffPropertyNode>, Node)
		SLATE_ARGUMENT(TWeakPtr<STableDiffStructureTree>, TreeWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Node = InArgs._Node;
		TreeWidget = InArgs._TreeWidget;
		SMultiColumnTableRow<TSharedPtr<FTableDiffPropertyNode>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (TSharedPtr<STableDiffStructureTree> Tree = TreeWidget.Pin())
		{
			if (ColumnName == TableDiffStructureColumns::Name)
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SExpanderArrow, SharedThis(this))
						.IndentAmount(14.f)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						Tree->MakeCell(Node, ColumnName)
					];
			}
			return Tree->MakeCell(Node, ColumnName);
		}
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FTableDiffPropertyNode> Node;
	TWeakPtr<STableDiffStructureTree> TreeWidget;
};

void STableDiffStructureTree::Construct(const FArguments& InArgs)
{
	Rows = InArgs._Rows;
	OnNodeSelected = InArgs._OnNodeSelected;

	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
		+ SHeaderRow::Column(TableDiffStructureColumns::Name)
		.DefaultLabel(LOCTEXT("Property", "Property"))
		.ManualWidth(TAttribute<float>::CreateLambda([this]() { return NameColumnWidth; }))
		.OnWidthChanged(FOnWidthChanged::CreateLambda([this](float NewWidth) { NameColumnWidth = FMath::Max(NewWidth, 120.f); }))
		+ SHeaderRow::Column(TableDiffStructureColumns::History)
		.DefaultLabel(LOCTEXT("History", "History"))
		.FillWidth(1.f)
		+ SHeaderRow::Column(TableDiffStructureColumns::Current)
		.DefaultLabel(LOCTEXT("Current", "Current"))
		.FillWidth(1.f);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.f)
		[
			SAssignNew(TreeView, STreeView<TSharedPtr<FTableDiffPropertyNode>>)
			.TreeItemsSource(&VisibleRoots)
			.OnGenerateRow(this, &STableDiffStructureTree::OnGenerateRow)
			.OnGetChildren(this, &STableDiffStructureTree::OnGetChildren)
			.OnSelectionChanged(this, &STableDiffStructureTree::OnSelectionChanged)
			.SelectionMode(ESelectionMode::Single)
			.HeaderRow(Header)
		]
	];
}

void STableDiffStructureTree::Refresh(const FString& InFilterText, bool bInChangesOnly)
{
	FilterText = InFilterText;
	bChangesOnly = bInChangesOnly;
	bUseFilter = FilterText.IsEmpty() == false || bChangesOnly;
	VisibleRoots.Reset();
	FilteredChildren.Reset();

	if (Rows != nullptr)
	{
		for (const TSharedPtr<FTableDiffAlignedRow>& Row : *Rows)
		{
			if (Row.IsValid() == false || Row->StructureNode.IsValid() == false)
			{
				continue;
			}

			TArray<TSharedPtr<FTableDiffPropertyNode>> Children;
			if (KeepNode(Row->StructureNode, false, Children))
			{
				VisibleRoots.Add(Row->StructureNode);
				FilteredChildren.Add(Row->StructureNode, Children);
			}
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		for (const TSharedPtr<FTableDiffPropertyNode>& Root : VisibleRoots)
		{
			ExpandChangedNodes(Root);
		}
	}
}

void STableDiffStructureTree::Reveal(FName RowName, const FString& FieldPath)
{
	if (Rows == nullptr || TreeView.IsValid() == false)
	{
		return;
	}

	TSharedPtr<FTableDiffPropertyNode> Target;
	for (const TSharedPtr<FTableDiffAlignedRow>& Row : *Rows)
	{
		if (Row.IsValid() && Row->StructureNode.IsValid())
		{
			Target = FindNode(Row->StructureNode, RowName, FieldPath);
			if (Target.IsValid())
			{
				break;
			}
		}
	}
	if (Target.IsValid() == false)
	{
		return;
	}

	bApplyingReveal = true;
	for (const TSharedPtr<FTableDiffPropertyNode>& Root : VisibleRoots)
	{
		if (ExpandToNode(Root, Target))
		{
			break;
		}
	}
	TreeView->SetSelection(Target, ESelectInfo::Direct);
	TreeView->RequestScrollIntoView(Target);
	bApplyingReveal = false;
}

TSharedRef<ITableRow> STableDiffStructureTree::OnGenerateRow(TSharedPtr<FTableDiffPropertyNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableDiffStructureRow, OwnerTable)
		.Node(Node)
		.TreeWidget(SharedThis(this));
}

void STableDiffStructureTree::OnGetChildren(TSharedPtr<FTableDiffPropertyNode> Node, TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren) const
{
	GetVisibleChildren(Node, OutChildren);
}

void STableDiffStructureTree::OnSelectionChanged(TSharedPtr<FTableDiffPropertyNode> Node, ESelectInfo::Type SelectInfo)
{
	if (bApplyingReveal || Node.IsValid() == false)
	{
		return;
	}
	OnNodeSelected.ExecuteIfBound(Node);
}

TSharedRef<SWidget> STableDiffStructureTree::MakeCell(TSharedPtr<FTableDiffPropertyNode> Node, FName ColumnId)
{
	FText Text;
	FText Tooltip;
	if (Node.IsValid() == false)
	{
		return SNullWidget::NullWidget;
	}

	if (ColumnId == TableDiffStructureColumns::Name)
	{
		Text = Node->DisplayName;
		Tooltip = Node->DisplayName;
	}
	else if (ColumnId == TableDiffStructureColumns::History)
	{
		Text = FText::FromString(Node->OldValue);
		Tooltip = Text;
	}
	else
	{
		Text = FText::FromString(Node->NewValue);
		Tooltip = Text;
	}

	TWeakPtr<STableDiffStructureTree> WeakThis = SharedThis(this);
	const TSharedPtr<FTableDiffPropertyNode> NodePtr = Node;
	FLinearColor TextColor = FLinearColor::White;
	if (Node->RowKind == ETableDiffRowKind::Added)
	{
		TextColor = FLinearColor(0.55f, 0.95f, 0.6f);
	}
	else if (Node->RowKind == ETableDiffRowKind::Removed)
	{
		TextColor = FLinearColor(1.f, 0.55f, 0.55f);
	}
	else if (Node->bValueDiffers || Node->bChildDiffers)
	{
		TextColor = FLinearColor(0.95f, 0.82f, 0.35f);
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor_Lambda([WeakThis, NodePtr, ColumnId]()
		{
			if (TSharedPtr<STableDiffStructureTree> Tree = WeakThis.Pin())
			{
				return Tree->GetCellBackground(NodePtr, ColumnId);
			}
			return FSlateColor(FLinearColor::Transparent);
		})
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(STextBlock)
			.Text(Text)
			.ToolTipText(Tooltip)
			.ColorAndOpacity(TextColor)
			.Font(Node->bIsRow ? FAppStyle::GetFontStyle("NormalFontBold") : FAppStyle::GetFontStyle("NormalFont"))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		];
}

FSlateColor STableDiffStructureTree::GetCellBackground(TSharedPtr<FTableDiffPropertyNode> Node, FName ColumnId) const
{
	if (Node.IsValid() == false)
	{
		return FSlateColor(FLinearColor::Transparent);
	}

	const bool bSelected = TreeView.IsValid() && TreeView->GetSelectedItems().Contains(Node);
	const FLinearColor FocusBoost = bSelected ? FLinearColor(0.06f, 0.08f, 0.12f, 0.f) : FLinearColor::Transparent;
	const bool bValueColumn = ColumnId != TableDiffStructureColumns::Name;

	if (Node->bValueDiffers && (bValueColumn || Node->bIsRow))
	{
		if (Node->RowKind == ETableDiffRowKind::Added && ColumnId != TableDiffStructureColumns::History)
		{
			return FSlateColor(FLinearColor(0.05f, 0.22f, 0.08f) + FocusBoost);
		}
		if (Node->RowKind == ETableDiffRowKind::Removed && ColumnId != TableDiffStructureColumns::Current)
		{
			return FSlateColor(FLinearColor(0.26f, 0.06f, 0.06f) + FocusBoost);
		}
		if (Node->RowKind == ETableDiffRowKind::Modified || Node->bIsRow == false)
		{
			return FSlateColor(FLinearColor(0.28f, 0.20f, 0.04f) + FocusBoost);
		}
	}

	if (Node->bChildDiffers)
	{
		return FSlateColor(FLinearColor(0.16f, 0.13f, 0.04f) + FocusBoost);
	}
	if (Node->RowKind == ETableDiffRowKind::Added && ColumnId != TableDiffStructureColumns::History)
	{
		return FSlateColor(FLinearColor(0.04f, 0.12f, 0.06f) + FocusBoost);
	}
	if (Node->RowKind == ETableDiffRowKind::Removed && ColumnId != TableDiffStructureColumns::Current)
	{
		return FSlateColor(FLinearColor(0.14f, 0.04f, 0.04f) + FocusBoost);
	}
	if (bSelected)
	{
		return FSlateColor(FLinearColor(0.08f, 0.12f, 0.2f));
	}
	return FSlateColor(FLinearColor::Transparent);
}

bool STableDiffStructureTree::KeepNode(const TSharedPtr<FTableDiffPropertyNode>& Node, bool bWholeRowChange, TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren)
{
	if (Node.IsValid() == false)
	{
		return false;
	}

	const bool bThisWholeRow = bWholeRowChange || (Node->bIsRow && (Node->RowKind == ETableDiffRowKind::Added || Node->RowKind == ETableDiffRowKind::Removed));
	TArray<TSharedPtr<FTableDiffPropertyNode>> KeptChildren;
	for (const TSharedPtr<FTableDiffPropertyNode>& Child : Node->Children)
	{
		TArray<TSharedPtr<FTableDiffPropertyNode>> GrandChildren;
		if (KeepNode(Child, bThisWholeRow, GrandChildren))
		{
			KeptChildren.Add(Child);
			FilteredChildren.Add(Child, GrandChildren);
		}
	}

	const bool bMatchesFilter = FilterText.IsEmpty()
		|| Node->DisplayName.ToString().Contains(FilterText, ESearchCase::IgnoreCase)
		|| Node->OldValue.Contains(FilterText, ESearchCase::IgnoreCase)
		|| Node->NewValue.Contains(FilterText, ESearchCase::IgnoreCase)
		|| Node->RowName.ToString().Contains(FilterText, ESearchCase::IgnoreCase)
		|| KeptChildren.Num() > 0;
	const bool bIsChange = Node->bValueDiffers || Node->bChildDiffers || bThisWholeRow;
	const bool bMatchesChangeFilter = bChangesOnly == false || bIsChange || KeptChildren.Num() > 0;
	if (bMatchesFilter && bMatchesChangeFilter)
	{
		OutChildren = KeptChildren;
		return true;
	}
	return false;
}

void STableDiffStructureTree::ExpandChangedNodes(const TSharedPtr<FTableDiffPropertyNode>& Node)
{
	if (Node.IsValid() == false || TreeView.IsValid() == false)
	{
		return;
	}

	const bool bExpand = Node->bIsRow ? Node->RowKind != ETableDiffRowKind::Unchanged : Node->bChildDiffers;
	if (bExpand)
	{
		TreeView->SetItemExpansion(Node, true);
	}

	TArray<TSharedPtr<FTableDiffPropertyNode>> Children;
	GetVisibleChildren(Node, Children);
	for (const TSharedPtr<FTableDiffPropertyNode>& Child : Children)
	{
		ExpandChangedNodes(Child);
	}
}

TSharedPtr<FTableDiffPropertyNode> STableDiffStructureTree::FindNode(const TSharedPtr<FTableDiffPropertyNode>& Node, FName RowName, const FString& FieldPath) const
{
	if (Node.IsValid() == false)
	{
		return nullptr;
	}
	if (Node->RowName == RowName && Node->Path == FieldPath)
	{
		return Node;
	}
	for (const TSharedPtr<FTableDiffPropertyNode>& Child : Node->Children)
	{
		if (TSharedPtr<FTableDiffPropertyNode> Found = FindNode(Child, RowName, FieldPath))
		{
			return Found;
		}
	}
	return nullptr;
}

bool STableDiffStructureTree::ExpandToNode(const TSharedPtr<FTableDiffPropertyNode>& Node, const TSharedPtr<FTableDiffPropertyNode>& Target)
{
	if (Node.IsValid() == false || Target.IsValid() == false)
	{
		return false;
	}
	if (Node == Target)
	{
		return true;
	}

	TArray<TSharedPtr<FTableDiffPropertyNode>> Children;
	GetVisibleChildren(Node, Children);
	for (const TSharedPtr<FTableDiffPropertyNode>& Child : Children)
	{
		if (ExpandToNode(Child, Target))
		{
			TreeView->SetItemExpansion(Node, true);
			return true;
		}
	}
	return false;
}

void STableDiffStructureTree::GetVisibleChildren(const TSharedPtr<FTableDiffPropertyNode>& Node, TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren) const
{
	if (Node.IsValid() == false)
	{
		return;
	}
	if (bUseFilter)
	{
		if (const TArray<TSharedPtr<FTableDiffPropertyNode>>* Filtered = FilteredChildren.Find(Node))
		{
			OutChildren.Append(*Filtered);
		}
		return;
	}
	OutChildren.Append(Node->Children);
}

#undef LOCTEXT_NAMESPACE
