#pragma once

#include "CoreMinimal.h"

class FTableDiffColumn
{
public:
	FName ColumnId;
	FName HeaderId;
	FText DisplayName;
	float Width = 120.f;
};

class FTableDiffCell
{
public:
	FText OldText;
	FText NewText;
	bool bDiffers = false;
	bool bOldPresent = false;
	bool bNewPresent = false;
};

enum class ETableDiffRowKind : uint8
{
	Unchanged,
	Modified,
	Added,
	Removed
};

enum class ETableDiffSide : uint8
{
	Old,
	New
};

class FTableDiffPropertyNode
{
public:
	FName RowName;
	FString Path;
	FText DisplayName;
	FString OldValue;
	FString NewValue;
	bool bIsRow = false;
	bool bValueDiffers = false;
	bool bChildDiffers = false;
	bool bOldPresent = false;
	bool bNewPresent = false;
	ETableDiffRowKind RowKind = ETableDiffRowKind::Unchanged;
	TArray<TSharedPtr<FTableDiffPropertyNode>> Children;
};

class FTableDiffAlignedRow
{
public:
	FName RowName;
	int32 OldIndex = INDEX_NONE;
	int32 NewIndex = INDEX_NONE;
	ETableDiffRowKind Kind = ETableDiffRowKind::Unchanged;
	TMap<FName, FTableDiffCell> Cells;
	TSharedPtr<FTableDiffPropertyNode> StructureNode;
};

class FTableDiffEntry
{
public:
	FText Label;
	FText Tooltip;
	FLinearColor Color = FLinearColor::White;
	FName RowName = NAME_None;
	FName ColumnId = NAME_None;
	FString OldValue;
	FString NewValue;
	FString FieldPath;
	bool bNavigable = false;
	TArray<TSharedPtr<FTableDiffEntry>> Children;
};

class FTableDiffResult
{
public:
	TArray<FTableDiffColumn> Columns;
	TArray<TSharedPtr<FTableDiffAlignedRow>> Rows;
	TArray<TSharedPtr<FTableDiffEntry>> DiffRoots;
	float RowNameWidth = 160.f;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;
	int32 ModifiedCount = 0;
};

namespace TableDiffColumns
{
	inline const FName RowNumber(TEXT("RowNumber"));
	inline const FName RowName(TEXT("RowName"));
}
