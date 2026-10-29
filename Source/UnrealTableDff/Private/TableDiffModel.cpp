#include "TableDiffModel.h"

#include "DataTableUtils.h"
#include "Engine/DataTable.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "TableDiffModel"

namespace TableDiffModelPrivate
{
	struct FFieldChange
	{
		FName ColumnId;
		FString Path;
		FString OldValue;
		FString NewValue;
	};

	struct FTableSide
	{
		const UScriptStruct* Struct = nullptr;
		TArray<FName> RowOrder;
		TMap<FName, int32> RowIndex;
		TMap<FName, const uint8*> RowData;
		TMap<FName, const FProperty*> Properties;
	};

	const FLinearColor AddedColor(0.45f, 0.90f, 0.50f);
	const FLinearColor RemovedColor(1.0f, 0.45f, 0.45f);
	const FLinearColor ModifiedColor(0.85f, 0.71f, 0.25f);
	const FLinearColor OrderColor(0.45f, 0.70f, 1.0f);
	const FLinearColor NeutralColor(0.65f, 0.65f, 0.65f);

	bool ShouldShowColumn(const FProperty* Prop)
	{
		if (Prop == nullptr)
		{
			return false;
		}
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated))
		{
			return false;
		}
		if (Prop->HasMetaData(TEXT("HideFromDataTableEditorColumn")))
		{
			return false;
		}
		return true;
	}

	FString ExportContainer(const FProperty* Prop, const uint8* Container)
	{
		if (Prop == nullptr || Container == nullptr)
		{
			return FString();
		}
		FString Result;
		Prop->ExportTextItem_InContainer(Result, Container, nullptr, nullptr, PPF_ExternalEditor);
		return Result;
	}

	FString ExportDirect(const FProperty* Prop, const uint8* Value)
	{
		if (Prop == nullptr || Value == nullptr)
		{
			return FString();
		}
		FString Result;
		Prop->ExportTextItem_Direct(Result, Value, nullptr, nullptr, PPF_ExternalEditor);
		return Result;
	}

	FText MakeDisplayText(const FProperty* Prop, const uint8* RowData)
	{
		if (Prop == nullptr || RowData == nullptr)
		{
			return FText::GetEmpty();
		}
		const FText PrettyText = DataTableUtils::GetPropertyValueAsText(Prop, RowData);
		if (PrettyText.IsEmpty() == false)
		{
			return PrettyText;
		}
		return FText::FromString(ExportContainer(Prop, RowData));
	}

	FText MakeColumnDisplayName(const FProperty* Prop)
	{
		if (Prop == nullptr)
		{
			return FText::GetEmpty();
		}
		const bool bBoolProperty = Prop->IsA<FBoolProperty>();
		return DataTableUtils::GetPropertyDisplayName(Prop, FName::NameToDisplayString(Prop->GetName(), bBoolProperty));
	}

	void CollectProperties(const UScriptStruct* Struct, TMap<FName, const FProperty*>& OutProperties, TArray<FName>* OutOrder)
	{
		if (Struct == nullptr)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop->HasAnyPropertyFlags(CPF_Deprecated))
			{
				continue;
			}
			OutProperties.FindOrAdd(Prop->GetFName()) = Prop;
			if (OutOrder != nullptr && OutOrder->Contains(Prop->GetFName()) == false)
			{
				OutOrder->Add(Prop->GetFName());
			}
		}
	}

	FTableSide MakeSide(const UDataTable* Table)
	{
		FTableSide Side;
		if (Table == nullptr)
		{
			return Side;
		}

		Side.Struct = Table->GetRowStruct();
		TArray<FName> UnusedOrder;
		CollectProperties(Side.Struct, Side.Properties, &UnusedOrder);

		int32 Index = 0;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			Side.RowOrder.Add(Pair.Key);
			Side.RowIndex.Add(Pair.Key, Index);
			Side.RowData.Add(Pair.Key, Pair.Value);
			++Index;
		}
		return Side;
	}

	void GrowWidth(float& Width, const FText& Text, const TSharedPtr<FSlateFontMeasure>& Measure, const FSlateFontInfo& Font, float MinWidth, float MaxWidth)
	{
		if (Measure.IsValid() == false || Text.IsEmpty())
		{
			Width = FMath::Clamp(Width, MinWidth, MaxWidth);
			return;
		}
		const float Measured = static_cast<float>(Measure->Measure(Text, Font).X) + 28.f;
		Width = FMath::Clamp(FMath::Max(Width, Measured), MinWidth, MaxWidth);
	}

	FString TruncateValue(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return TEXT("(empty)");
		}
		FString SingleLine = Value;
		SingleLine.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		SingleLine.ReplaceInline(TEXT("\n"), TEXT(" "));
		SingleLine.ReplaceInline(TEXT("\r"), TEXT(" "));
		constexpr int32 MaxLen = 72;
		if (SingleLine.Len() > MaxLen)
		{
			return SingleLine.Left(MaxLen - 3) + TEXT("...");
		}
		return SingleLine;
	}

	FString JoinRowOrder(const TArray<FName>& Names)
	{
		FString Result;
		const int32 Count = FMath::Min(Names.Num(), 100);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Index > 0)
			{
				Result += TEXT("\n");
			}
			Result += FString::Printf(TEXT("%d. %s"), Index + 1, *Names[Index].ToString());
		}
		if (Names.Num() > Count)
		{
			Result += FString::Printf(TEXT("\n... %d more"), Names.Num() - Count);
		}
		return Result;
	}

	TSharedRef<FTableDiffEntry> MakeEntry(const FText& Label, const FText& Tooltip, const FLinearColor& Color, FName RowName, FName ColumnId, const FString& OldValue, const FString& NewValue, bool bNavigable, const FString& FieldPath = FString())
	{
		TSharedRef<FTableDiffEntry> Entry = MakeShared<FTableDiffEntry>();
		Entry->Label = Label;
		Entry->Tooltip = Tooltip;
		Entry->Color = Color;
		Entry->RowName = RowName;
		Entry->ColumnId = ColumnId;
		Entry->OldValue = OldValue;
		Entry->NewValue = NewValue;
		Entry->FieldPath = FieldPath;
		Entry->bNavigable = bNavigable;
		return Entry;
	}

	void DiffStructs(const UScriptStruct* OldStruct, const uint8* OldData, const UScriptStruct* NewStruct, const uint8* NewData, const FString& Path, FName ColumnId, TArray<FFieldChange>& Changes, int32 Depth);

	void AddChange(TArray<FFieldChange>& Changes, FName ColumnId, const FString& Path, const FString& OldValue, const FString& NewValue)
	{
		FFieldChange Change;
		Change.ColumnId = ColumnId;
		Change.Path = Path;
		Change.OldValue = OldValue;
		Change.NewValue = NewValue;
		Changes.Add(MoveTemp(Change));
	}

	void DiffProperties(const FProperty* OldProp, const uint8* OldContainer, const FProperty* NewProp, const uint8* NewContainer, const FString& Path, FName ColumnId, TArray<FFieldChange>& Changes, int32 Depth)
	{
		if (OldProp == nullptr && NewProp == nullptr)
		{
			return;
		}

		const FString OldText = ExportContainer(OldProp, OldContainer);
		const FString NewText = ExportContainer(NewProp, NewContainer);
		if (OldText == NewText)
		{
			return;
		}

		if (Depth >= 8 || OldProp == nullptr || NewProp == nullptr || OldContainer == nullptr || NewContainer == nullptr)
		{
			AddChange(Changes, ColumnId, Path, OldText, NewText);
			return;
		}

		if (OldProp->ArrayDim > 1 || NewProp->ArrayDim > 1)
		{
			const int32 Before = Changes.Num();
			const int32 Count = FMath::Max(OldProp->ArrayDim, NewProp->ArrayDim);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FString ElementPath = FString::Printf(TEXT("%s[%d]"), *Path, Index);
				const bool bHasOld = Index < OldProp->ArrayDim;
				const bool bHasNew = Index < NewProp->ArrayDim;
				const uint8* OldValue = bHasOld ? OldProp->ContainerPtrToValuePtr<uint8>(OldContainer, Index) : nullptr;
				const uint8* NewValue = bHasNew ? NewProp->ContainerPtrToValuePtr<uint8>(NewContainer, Index) : nullptr;
				const FString OldElement = ExportDirect(OldProp, OldValue);
				const FString NewElement = ExportDirect(NewProp, NewValue);
				if (OldElement == NewElement)
				{
					continue;
				}

				const FStructProperty* OldStructProp = CastField<FStructProperty>(OldProp);
				const FStructProperty* NewStructProp = CastField<FStructProperty>(NewProp);
				if (bHasOld && bHasNew && OldStructProp != nullptr && NewStructProp != nullptr && OldStructProp->Struct != nullptr && NewStructProp->Struct != nullptr)
				{
					const int32 InnerBefore = Changes.Num();
					DiffStructs(OldStructProp->Struct, OldValue, NewStructProp->Struct, NewValue, ElementPath, ColumnId, Changes, Depth + 1);
					if (Changes.Num() == InnerBefore)
					{
						AddChange(Changes, ColumnId, ElementPath, OldElement, NewElement);
					}
					continue;
				}

				AddChange(Changes, ColumnId, ElementPath, OldElement, NewElement);
			}
			if (Changes.Num() == Before)
			{
				AddChange(Changes, ColumnId, Path, OldText, NewText);
			}
			return;
		}

		const FStructProperty* OldStructProp = CastField<FStructProperty>(OldProp);
		const FStructProperty* NewStructProp = CastField<FStructProperty>(NewProp);
		if (OldStructProp != nullptr && NewStructProp != nullptr && OldStructProp->Struct != nullptr && NewStructProp->Struct != nullptr)
		{
			const int32 Before = Changes.Num();
			DiffStructs(
				OldStructProp->Struct,
				OldStructProp->ContainerPtrToValuePtr<uint8>(OldContainer),
				NewStructProp->Struct,
				NewStructProp->ContainerPtrToValuePtr<uint8>(NewContainer),
				Path,
				ColumnId,
				Changes,
				Depth + 1);
			if (Changes.Num() == Before)
			{
				AddChange(Changes, ColumnId, Path, OldText, NewText);
			}
			return;
		}

		const FArrayProperty* OldArray = CastField<FArrayProperty>(OldProp);
		const FArrayProperty* NewArray = CastField<FArrayProperty>(NewProp);
		if (OldArray != nullptr && NewArray != nullptr && OldArray->Inner != nullptr && NewArray->Inner != nullptr)
		{
			FScriptArrayHelper OldHelper(OldArray, OldArray->ContainerPtrToValuePtr<void>(OldContainer));
			FScriptArrayHelper NewHelper(NewArray, NewArray->ContainerPtrToValuePtr<void>(NewContainer));
			const int32 Before = Changes.Num();
			const int32 Count = FMath::Max(OldHelper.Num(), NewHelper.Num());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FString ElementPath = FString::Printf(TEXT("%s[%d]"), *Path, Index);
				if (Index >= OldHelper.Num())
				{
					AddChange(Changes, ColumnId, ElementPath, FString(), ExportDirect(NewArray->Inner, NewHelper.GetRawPtr(Index)));
					continue;
				}
				if (Index >= NewHelper.Num())
				{
					AddChange(Changes, ColumnId, ElementPath, ExportDirect(OldArray->Inner, OldHelper.GetRawPtr(Index)), FString());
					continue;
				}

				const uint8* OldElementData = OldHelper.GetRawPtr(Index);
				const uint8* NewElementData = NewHelper.GetRawPtr(Index);
				const FStructProperty* OldInnerStruct = CastField<FStructProperty>(OldArray->Inner);
				const FStructProperty* NewInnerStruct = CastField<FStructProperty>(NewArray->Inner);
				if (OldInnerStruct != nullptr && NewInnerStruct != nullptr && OldInnerStruct->Struct != nullptr && NewInnerStruct->Struct != nullptr)
				{
					const int32 InnerBefore = Changes.Num();
					DiffStructs(OldInnerStruct->Struct, OldElementData, NewInnerStruct->Struct, NewElementData, ElementPath, ColumnId, Changes, Depth + 1);
					if (Changes.Num() == InnerBefore)
					{
						AddChange(Changes, ColumnId, ElementPath, ExportDirect(OldArray->Inner, OldElementData), ExportDirect(NewArray->Inner, NewElementData));
					}
					continue;
				}

				const FString OldElement = ExportDirect(OldArray->Inner, OldElementData);
				const FString NewElement = ExportDirect(NewArray->Inner, NewElementData);
				if (OldElement != NewElement)
				{
					AddChange(Changes, ColumnId, ElementPath, OldElement, NewElement);
				}
			}
			if (Changes.Num() == Before)
			{
				AddChange(Changes, ColumnId, Path, OldText, NewText);
			}
			return;
		}

		AddChange(Changes, ColumnId, Path, OldText, NewText);
	}

	void DiffStructs(const UScriptStruct* OldStruct, const uint8* OldData, const UScriptStruct* NewStruct, const uint8* NewData, const FString& Path, FName ColumnId, TArray<FFieldChange>& Changes, int32 Depth)
	{
		if (Depth >= 8 || OldStruct == nullptr || NewStruct == nullptr || OldData == nullptr || NewData == nullptr)
		{
			return;
		}

		TMap<FName, const FProperty*> OldProperties;
		TMap<FName, const FProperty*> NewProperties;
		TArray<FName> Names;
		CollectProperties(OldStruct, OldProperties, &Names);
		CollectProperties(NewStruct, NewProperties, &Names);

		for (const FName& Name : Names)
		{
			const FProperty* OldProp = OldProperties.FindRef(Name);
			const FProperty* NewProp = NewProperties.FindRef(Name);
			const FString ChildPath = Path + TEXT(".") + Name.ToString();
			if (OldProp == nullptr || NewProp == nullptr)
			{
				AddChange(
					Changes,
					ColumnId,
					ChildPath,
					ExportContainer(OldProp, OldData),
					ExportContainer(NewProp, NewData));
				continue;
			}
			DiffProperties(OldProp, OldData, NewProp, NewData, ChildPath, ColumnId, Changes, Depth);
		}
	}

	void FinalizeStructureNode(const TSharedRef<FTableDiffPropertyNode>& Node, bool bCompare)
	{
		Node->bChildDiffers = false;
		for (const TSharedPtr<FTableDiffPropertyNode>& Child : Node->Children)
		{
			if (Child.IsValid() && (Child->bValueDiffers || Child->bChildDiffers))
			{
				Node->bChildDiffers = true;
				break;
			}
		}

		if (Node->Children.Num() > 0 || bCompare == false)
		{
			Node->bValueDiffers = false;
			return;
		}

		Node->bValueDiffers = Node->OldValue != Node->NewValue;
	}

	TSharedRef<FTableDiffPropertyNode> BuildPropertyNode(
		const FProperty* OldProp,
		const uint8* OldContainer,
		const FProperty* NewProp,
		const uint8* NewContainer,
		const FString& Path,
		const FText& DisplayName,
		FName RowName,
		ETableDiffRowKind RowKind,
		bool bCompare,
		int32 Depth);

	void AppendStructFields(
		const UScriptStruct* OldStruct,
		const uint8* OldData,
		const UScriptStruct* NewStruct,
		const uint8* NewData,
		const FString& ParentPath,
		FName RowName,
		ETableDiffRowKind RowKind,
		bool bCompare,
		int32 Depth,
		TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren)
	{
		TMap<FName, const FProperty*> OldProperties;
		TMap<FName, const FProperty*> NewProperties;
		TArray<FName> Names;
		CollectProperties(OldStruct, OldProperties, &Names);
		CollectProperties(NewStruct, NewProperties, &Names);

		for (const FName& Name : Names)
		{
			const FProperty* OldChild = OldProperties.FindRef(Name);
			const FProperty* NewChild = NewProperties.FindRef(Name);
			const FProperty* DisplayProp = NewChild != nullptr ? NewChild : OldChild;
			const FString ChildPath = ParentPath + TEXT(".") + Name.ToString();
			OutChildren.Add(BuildPropertyNode(
				OldChild,
				OldData,
				NewChild,
				NewData,
				ChildPath,
				MakeColumnDisplayName(DisplayProp),
				RowName,
				RowKind,
				bCompare,
				Depth));
		}
	}

	bool IsPlainStructProperty(const FProperty* Prop)
	{
		if (Prop == nullptr || Prop->ArrayDim != 1)
		{
			return false;
		}
		const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		return StructProp != nullptr && StructProp->Struct != nullptr;
	}

	bool IsDynamicArrayProperty(const FProperty* Prop)
	{
		return Prop != nullptr && Prop->ArrayDim == 1 && CastField<FArrayProperty>(Prop) != nullptr;
	}

	void AppendStaticArrayElements(
		const FProperty* OldProp,
		const uint8* OldContainer,
		const FProperty* NewProp,
		const uint8* NewContainer,
		const FString& Path,
		FName RowName,
		ETableDiffRowKind RowKind,
		bool bCompare,
		int32 Depth,
		TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren)
	{
		const int32 OldCount = (OldProp != nullptr && OldContainer != nullptr) ? OldProp->ArrayDim : 0;
		const int32 NewCount = (NewProp != nullptr && NewContainer != nullptr) ? NewProp->ArrayDim : 0;
		const int32 Count = FMath::Max(OldCount, NewCount);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const uint8* OldElement = Index < OldCount ? OldProp->ContainerPtrToValuePtr<uint8>(OldContainer, Index) : nullptr;
			const uint8* NewElement = Index < NewCount ? NewProp->ContainerPtrToValuePtr<uint8>(NewContainer, Index) : nullptr;
			TSharedRef<FTableDiffPropertyNode> Element = MakeShared<FTableDiffPropertyNode>();
			Element->RowName = RowName;
			Element->RowKind = RowKind;
			Element->Path = FString::Printf(TEXT("%s[%d]"), *Path, Index);
			Element->DisplayName = FText::FromString(FString::Printf(TEXT("[%d]"), Index));
			Element->bOldPresent = OldElement != nullptr;
			Element->bNewPresent = NewElement != nullptr;
			Element->OldValue = ExportDirect(OldProp, OldElement);
			Element->NewValue = ExportDirect(NewProp, NewElement);

			const FStructProperty* OldStruct = CastField<FStructProperty>(OldProp);
			const FStructProperty* NewStruct = CastField<FStructProperty>(NewProp);
			const bool bOldStruct = OldStruct != nullptr && OldStruct->Struct != nullptr && OldElement != nullptr;
			const bool bNewStruct = NewStruct != nullptr && NewStruct->Struct != nullptr && NewElement != nullptr;
			if (Depth + 1 < 8 && (bOldStruct || bNewStruct))
			{
				AppendStructFields(
					bOldStruct ? OldStruct->Struct : nullptr,
					OldElement,
					bNewStruct ? NewStruct->Struct : nullptr,
					NewElement,
					Element->Path,
					RowName,
					RowKind,
					bCompare,
					Depth + 1,
					Element->Children);
			}
			FinalizeStructureNode(Element, bCompare);
			OutChildren.Add(Element);
		}
	}

	void AppendDynamicArrayElements(
		const FProperty* OldProp,
		const uint8* OldContainer,
		const FProperty* NewProp,
		const uint8* NewContainer,
		const FString& Path,
		FName RowName,
		ETableDiffRowKind RowKind,
		bool bCompare,
		int32 Depth,
		TArray<TSharedPtr<FTableDiffPropertyNode>>& OutChildren)
	{
		const FArrayProperty* OldArray = CastField<FArrayProperty>(OldProp);
		const FArrayProperty* NewArray = CastField<FArrayProperty>(NewProp);
		TUniquePtr<FScriptArrayHelper> OldHelper;
		TUniquePtr<FScriptArrayHelper> NewHelper;
		int32 OldCount = 0;
		int32 NewCount = 0;
		if (OldArray != nullptr && OldArray->Inner != nullptr && OldContainer != nullptr)
		{
			OldHelper = MakeUnique<FScriptArrayHelper>(OldArray, OldArray->ContainerPtrToValuePtr<void>(OldContainer));
			OldCount = OldHelper->Num();
		}
		if (NewArray != nullptr && NewArray->Inner != nullptr && NewContainer != nullptr)
		{
			NewHelper = MakeUnique<FScriptArrayHelper>(NewArray, NewArray->ContainerPtrToValuePtr<void>(NewContainer));
			NewCount = NewHelper->Num();
		}

		const int32 Count = FMath::Max(OldCount, NewCount);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const uint8* OldElement = (OldHelper.IsValid() && Index < OldCount) ? OldHelper->GetRawPtr(Index) : nullptr;
			const uint8* NewElement = (NewHelper.IsValid() && Index < NewCount) ? NewHelper->GetRawPtr(Index) : nullptr;
			const FProperty* OldInner = OldArray != nullptr ? OldArray->Inner : nullptr;
			const FProperty* NewInner = NewArray != nullptr ? NewArray->Inner : nullptr;
			TSharedRef<FTableDiffPropertyNode> Element = MakeShared<FTableDiffPropertyNode>();
			Element->RowName = RowName;
			Element->RowKind = RowKind;
			Element->Path = FString::Printf(TEXT("%s[%d]"), *Path, Index);
			Element->DisplayName = FText::FromString(FString::Printf(TEXT("[%d]"), Index));
			Element->bOldPresent = OldElement != nullptr;
			Element->bNewPresent = NewElement != nullptr;
			Element->OldValue = ExportDirect(OldInner, OldElement);
			Element->NewValue = ExportDirect(NewInner, NewElement);

			const FStructProperty* OldStruct = CastField<FStructProperty>(OldInner);
			const FStructProperty* NewStruct = CastField<FStructProperty>(NewInner);
			const bool bOldStruct = OldStruct != nullptr && OldStruct->Struct != nullptr && OldElement != nullptr;
			const bool bNewStruct = NewStruct != nullptr && NewStruct->Struct != nullptr && NewElement != nullptr;
			if (Depth + 1 < 8 && (bOldStruct || bNewStruct))
			{
				AppendStructFields(
					bOldStruct ? OldStruct->Struct : nullptr,
					OldElement,
					bNewStruct ? NewStruct->Struct : nullptr,
					NewElement,
					Element->Path,
					RowName,
					RowKind,
					bCompare,
					Depth + 1,
					Element->Children);
			}
			FinalizeStructureNode(Element, bCompare);
			OutChildren.Add(Element);
		}
	}

	TSharedRef<FTableDiffPropertyNode> BuildPropertyNode(
		const FProperty* OldProp,
		const uint8* OldContainer,
		const FProperty* NewProp,
		const uint8* NewContainer,
		const FString& Path,
		const FText& DisplayName,
		FName RowName,
		ETableDiffRowKind RowKind,
		bool bCompare,
		int32 Depth)
	{
		TSharedRef<FTableDiffPropertyNode> Node = MakeShared<FTableDiffPropertyNode>();
		Node->RowName = RowName;
		Node->RowKind = RowKind;
		Node->Path = Path;
		Node->DisplayName = DisplayName;
		Node->bOldPresent = OldProp != nullptr && OldContainer != nullptr;
		Node->bNewPresent = NewProp != nullptr && NewContainer != nullptr;
		if (Node->bOldPresent)
		{
			Node->OldValue = MakeDisplayText(OldProp, OldContainer).ToString();
		}
		if (Node->bNewPresent)
		{
			Node->NewValue = MakeDisplayText(NewProp, NewContainer).ToString();
		}

		if (Depth < 8)
		{
			const bool bOldStaticArray = OldProp != nullptr && OldProp->ArrayDim > 1;
			const bool bNewStaticArray = NewProp != nullptr && NewProp->ArrayDim > 1;
			if (bOldStaticArray || bNewStaticArray)
			{
				const bool bCompatible = (bOldStaticArray == false || bNewStaticArray == false || OldProp->ArrayDim == NewProp->ArrayDim);
				if (bCompatible)
				{
					AppendStaticArrayElements(OldProp, OldContainer, NewProp, NewContainer, Path, RowName, RowKind, bCompare, Depth, Node->Children);
				}
			}
			else if (IsPlainStructProperty(OldProp) || IsPlainStructProperty(NewProp))
			{
				const bool bOldOk = OldProp == nullptr || OldContainer == nullptr || IsPlainStructProperty(OldProp);
				const bool bNewOk = NewProp == nullptr || NewContainer == nullptr || IsPlainStructProperty(NewProp);
				if (bOldOk && bNewOk)
				{
					const FStructProperty* OldStruct = CastField<FStructProperty>(OldProp);
					const FStructProperty* NewStruct = CastField<FStructProperty>(NewProp);
					AppendStructFields(
						(OldStruct != nullptr && OldContainer != nullptr) ? OldStruct->Struct : nullptr,
						(OldStruct != nullptr && OldContainer != nullptr) ? OldStruct->ContainerPtrToValuePtr<uint8>(OldContainer) : nullptr,
						(NewStruct != nullptr && NewContainer != nullptr) ? NewStruct->Struct : nullptr,
						(NewStruct != nullptr && NewContainer != nullptr) ? NewStruct->ContainerPtrToValuePtr<uint8>(NewContainer) : nullptr,
						Path,
						RowName,
						RowKind,
						bCompare,
						Depth + 1,
						Node->Children);
				}
			}
			else if (IsDynamicArrayProperty(OldProp) || IsDynamicArrayProperty(NewProp))
			{
				const bool bOldOk = OldProp == nullptr || OldContainer == nullptr || IsDynamicArrayProperty(OldProp);
				const bool bNewOk = NewProp == nullptr || NewContainer == nullptr || IsDynamicArrayProperty(NewProp);
				if (bOldOk && bNewOk)
				{
					AppendDynamicArrayElements(OldProp, OldContainer, NewProp, NewContainer, Path, RowName, RowKind, bCompare, Depth, Node->Children);
				}
			}
		}

		FinalizeStructureNode(Node, bCompare);
		return Node;
	}

	TSharedRef<FTableDiffPropertyNode> BuildRowStructureNode(
		const FTableDiffAlignedRow& Row,
		const TArray<FTableDiffColumn>& Columns,
		const FTableSide& OldSide,
		const FTableSide& NewSide,
		const uint8* OldData,
		const uint8* NewData,
		bool bCompare)
	{
		TSharedRef<FTableDiffPropertyNode> Root = MakeShared<FTableDiffPropertyNode>();
		Root->bIsRow = true;
		Root->RowName = Row.RowName;
		Root->RowKind = Row.Kind;
		Root->DisplayName = FText::FromName(Row.RowName);
		Root->bOldPresent = Row.OldIndex != INDEX_NONE;
		Root->bNewPresent = Row.NewIndex != INDEX_NONE;

		const uint8* OldContainer = Root->bOldPresent ? OldData : nullptr;
		const uint8* NewContainer = Root->bNewPresent ? NewData : nullptr;
		for (const FTableDiffColumn& Column : Columns)
		{
			const FProperty* OldProp = OldContainer != nullptr ? OldSide.Properties.FindRef(Column.ColumnId) : nullptr;
			const FProperty* NewProp = NewContainer != nullptr ? NewSide.Properties.FindRef(Column.ColumnId) : nullptr;
			if (OldProp == nullptr && NewProp == nullptr)
			{
				continue;
			}
			Root->Children.Add(BuildPropertyNode(
				OldProp,
				OldContainer,
				NewProp,
				NewContainer,
				Column.DisplayName.ToString(),
				Column.DisplayName,
				Row.RowName,
				Row.Kind,
				bCompare,
				0));
		}

		FinalizeStructureNode(Root, false);
		Root->bValueDiffers = Row.Kind != ETableDiffRowKind::Unchanged;
		return Root;
	}
}

FTableDiffResult FTableDiffModel::Build(const UDataTable* OldTable, const UDataTable* NewTable)
{
	using namespace TableDiffModelPrivate;

	FTableDiffResult Result;
	const FTableSide OldSide = MakeSide(OldTable);
	const FTableSide NewSide = MakeSide(NewTable);

	TSharedPtr<FSlateFontMeasure> FontMeasure;
	FSlateFontInfo Font;
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr)
	{
		FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		Font = FAppStyle::GetFontStyle("NormalFont");
	}

	TArray<FName> ColumnOrder;
	TSet<FName> UsedHeaderIds;
	UsedHeaderIds.Add(TableDiffColumns::RowNumber);
	UsedHeaderIds.Add(TableDiffColumns::RowName);

	auto AppendColumns = [&ColumnOrder](const UScriptStruct* Struct)
	{
		if (Struct == nullptr)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (ShouldShowColumn(*It) == false)
			{
				continue;
			}
			if (ColumnOrder.Contains(It->GetFName()) == false)
			{
				ColumnOrder.Add(It->GetFName());
			}
		}
	};
	AppendColumns(NewSide.Struct);
	AppendColumns(OldSide.Struct);

	for (const FName& ColumnId : ColumnOrder)
	{
		const FProperty* NewProp = NewSide.Properties.FindRef(ColumnId);
		const FProperty* OldProp = OldSide.Properties.FindRef(ColumnId);
		const FProperty* DisplayProp = NewProp != nullptr ? NewProp : OldProp;

		FTableDiffColumn Column;
		Column.ColumnId = ColumnId;
		Column.HeaderId = ColumnId;
		if (UsedHeaderIds.Contains(Column.HeaderId))
		{
			Column.HeaderId = FName(*FString::Printf(TEXT("Col_%s"), *ColumnId.ToString()));
			int32 Suffix = 2;
			while (UsedHeaderIds.Contains(Column.HeaderId))
			{
				Column.HeaderId = FName(*FString::Printf(TEXT("Col_%s_%d"), *ColumnId.ToString(), Suffix));
				++Suffix;
			}
		}
		UsedHeaderIds.Add(Column.HeaderId);
		Column.DisplayName = MakeColumnDisplayName(DisplayProp);
		Column.Width = 96.f;
		GrowWidth(Column.Width, Column.DisplayName, FontMeasure, Font, 72.f, 360.f);
		Result.Columns.Add(MoveTemp(Column));
	}

	TSet<FName> OldNames;
	OldNames.Append(OldSide.RowOrder);
	TSet<FName> NewNames;
	NewNames.Append(NewSide.RowOrder);
	TSet<FName> Emitted;
	TArray<TSharedPtr<FTableDiffAlignedRow>> AlignedRows;

	auto EmitRow = [&AlignedRows, &Emitted](FName RowName, int32 OldIndex, int32 NewIndex, ETableDiffRowKind Kind)
	{
		TSharedRef<FTableDiffAlignedRow> Row = MakeShared<FTableDiffAlignedRow>();
		Row->RowName = RowName;
		Row->OldIndex = OldIndex;
		Row->NewIndex = NewIndex;
		Row->Kind = Kind;
		AlignedRows.Add(Row);
		Emitted.Add(RowName);
	};

	int32 OldCursor = 0;
	int32 NewCursor = 0;
	while (OldCursor < OldSide.RowOrder.Num() || NewCursor < NewSide.RowOrder.Num())
	{
		while (OldCursor < OldSide.RowOrder.Num() && Emitted.Contains(OldSide.RowOrder[OldCursor]))
		{
			++OldCursor;
		}
		if (OldCursor >= OldSide.RowOrder.Num() && NewCursor >= NewSide.RowOrder.Num())
		{
			break;
		}
		if (OldCursor >= OldSide.RowOrder.Num())
		{
			const FName RowName = NewSide.RowOrder[NewCursor];
			const int32 NewIndex = NewSide.RowIndex.FindRef(RowName);
			EmitRow(RowName, INDEX_NONE, NewIndex, ETableDiffRowKind::Added);
			++NewCursor;
			continue;
		}
		if (NewCursor >= NewSide.RowOrder.Num())
		{
			const FName RowName = OldSide.RowOrder[OldCursor];
			const int32 OldIndex = OldSide.RowIndex.FindRef(RowName);
			EmitRow(RowName, OldIndex, INDEX_NONE, ETableDiffRowKind::Removed);
			++OldCursor;
			continue;
		}

		const FName OldName = OldSide.RowOrder[OldCursor];
		const FName NewName = NewSide.RowOrder[NewCursor];
		if (OldName == NewName)
		{
			EmitRow(OldName, OldSide.RowIndex.FindRef(OldName), NewSide.RowIndex.FindRef(NewName), ETableDiffRowKind::Unchanged);
			++OldCursor;
			++NewCursor;
			continue;
		}
		if (NewNames.Contains(OldName) == false)
		{
			EmitRow(OldName, OldSide.RowIndex.FindRef(OldName), INDEX_NONE, ETableDiffRowKind::Removed);
			++OldCursor;
			continue;
		}
		if (OldNames.Contains(NewName) == false)
		{
			EmitRow(NewName, INDEX_NONE, NewSide.RowIndex.FindRef(NewName), ETableDiffRowKind::Added);
			++NewCursor;
			continue;
		}

		EmitRow(NewName, OldSide.RowIndex.FindRef(NewName), NewSide.RowIndex.FindRef(NewName), ETableDiffRowKind::Unchanged);
		++NewCursor;
	}

	TArray<TSharedPtr<FTableDiffEntry>> RowEntries;
	for (const TSharedPtr<FTableDiffAlignedRow>& Row : AlignedRows)
	{
		const uint8* const* OldDataPtr = OldSide.RowData.Find(Row->RowName);
		const uint8* const* NewDataPtr = NewSide.RowData.Find(Row->RowName);
		const uint8* OldData = OldDataPtr != nullptr ? *OldDataPtr : nullptr;
		const uint8* NewData = NewDataPtr != nullptr ? *NewDataPtr : nullptr;

		GrowWidth(Result.RowNameWidth, FText::FromName(Row->RowName), FontMeasure, Font, 120.f, 280.f);

		TArray<FFieldChange> FieldChanges;
		for (FTableDiffColumn& Column : Result.Columns)
		{
			const FProperty* OldProp = OldSide.Properties.FindRef(Column.ColumnId);
			const FProperty* NewProp = NewSide.Properties.FindRef(Column.ColumnId);

			FTableDiffCell Cell;
			if (OldProp != nullptr && OldData != nullptr && Row->OldIndex != INDEX_NONE)
			{
				Cell.OldText = MakeDisplayText(OldProp, OldData);
				Cell.bOldPresent = true;
				GrowWidth(Column.Width, Cell.OldText, FontMeasure, Font, 72.f, 360.f);
			}
			if (NewProp != nullptr && NewData != nullptr && Row->NewIndex != INDEX_NONE)
			{
				Cell.NewText = MakeDisplayText(NewProp, NewData);
				Cell.bNewPresent = true;
				GrowWidth(Column.Width, Cell.NewText, FontMeasure, Font, 72.f, 360.f);
			}

			if (Row->Kind == ETableDiffRowKind::Unchanged)
			{
				if (OldProp == nullptr || NewProp == nullptr || OldData == nullptr || NewData == nullptr)
				{
					const FString OldValue = ExportContainer(OldProp, OldData);
					const FString NewValue = ExportContainer(NewProp, NewData);
					if (OldProp != nullptr || NewProp != nullptr)
					{
						if (OldValue != NewValue)
						{
							AddChange(FieldChanges, Column.ColumnId, Column.DisplayName.ToString(), OldValue, NewValue);
							Cell.bDiffers = true;
						}
					}
				}
				else
				{
					const int32 Before = FieldChanges.Num();
					DiffProperties(OldProp, OldData, NewProp, NewData, Column.DisplayName.ToString(), Column.ColumnId, FieldChanges, 0);
					if (FieldChanges.Num() != Before)
					{
						Cell.bDiffers = true;
					}
				}
			}

			Row->Cells.Add(Column.ColumnId, MoveTemp(Cell));
		}

		if (Row->Kind == ETableDiffRowKind::Added)
		{
			++Result.AddedCount;
			const FText Label = FText::Format(LOCTEXT("AddedRow", "Added row {0}"), FText::FromName(Row->RowName));
			RowEntries.Add(MakeEntry(Label, Label, AddedColor, Row->RowName, NAME_None, FString(), TEXT("This row exists only in the current asset."), true));
		}
		else if (Row->Kind == ETableDiffRowKind::Removed)
		{
			++Result.RemovedCount;
			const FText Label = FText::Format(LOCTEXT("RemovedRow", "Removed row {0}"), FText::FromName(Row->RowName));
			RowEntries.Add(MakeEntry(Label, Label, RemovedColor, Row->RowName, NAME_None, TEXT("This row exists only in the selected revision."), FString(), true));
		}
		else if (FieldChanges.Num() > 0)
		{
			Row->Kind = ETableDiffRowKind::Modified;
			++Result.ModifiedCount;
			const FText Label = FText::Format(LOCTEXT("ModifiedRow", "Modified row {0} ({1})"), FText::FromName(Row->RowName), FText::AsNumber(FieldChanges.Num()));
			TSharedRef<FTableDiffEntry> Parent = MakeEntry(
				Label,
				Label,
				ModifiedColor,
				Row->RowName,
				NAME_None,
				TEXT("Select a field below to compare values."),
				TEXT("Select a field below to compare values."),
				true);
			for (const FFieldChange& Change : FieldChanges)
			{
				const FText FieldLabel = FText::Format(
					LOCTEXT("FieldLabel", "{0}: {1} -> {2}"),
					FText::FromString(Change.Path),
					FText::FromString(TruncateValue(Change.OldValue)),
					FText::FromString(TruncateValue(Change.NewValue)));
				const FString OldShown = Change.OldValue.IsEmpty() ? FString(TEXT("(empty)")) : Change.OldValue;
				const FString NewShown = Change.NewValue.IsEmpty() ? FString(TEXT("(empty)")) : Change.NewValue;
				const FText FieldTip = FText::Format(
					LOCTEXT("FieldTip", "{0}\nOld: {1}\nNew: {2}"),
					FText::FromString(Change.Path),
					FText::FromString(OldShown),
					FText::FromString(NewShown));
				Parent->Children.Add(MakeEntry(FieldLabel, FieldTip, ModifiedColor, Row->RowName, Change.ColumnId, OldShown, NewShown, true, Change.Path));
			}
			RowEntries.Add(Parent);
		}

		const bool bCompareChildren = Row->Kind != ETableDiffRowKind::Added && Row->Kind != ETableDiffRowKind::Removed;
		Row->StructureNode = BuildRowStructureNode(*Row, Result.Columns, OldSide, NewSide, OldData, NewData, bCompareChildren);
	}

	Result.Rows = MoveTemp(AlignedRows);

	const FString OldStructPath = OldSide.Struct != nullptr ? OldSide.Struct->GetPathName() : FString(TEXT("(none)"));
	const FString NewStructPath = NewSide.Struct != nullptr ? NewSide.Struct->GetPathName() : FString(TEXT("(none)"));
	if (OldStructPath != NewStructPath)
	{
		const FText Label = LOCTEXT("StructChanged", "Row structure changed");
		const FText Tooltip = FText::Format(LOCTEXT("StructChangedTip", "Old: {0}\nNew: {1}"), FText::FromString(OldStructPath), FText::FromString(NewStructPath));
		Result.DiffRoots.Add(MakeEntry(Label, Tooltip, ModifiedColor, NAME_None, NAME_None, OldStructPath, NewStructPath, true));
	}

	TArray<FName> OldShared;
	TArray<FName> NewShared;
	for (const FName& Name : OldSide.RowOrder)
	{
		if (NewNames.Contains(Name))
		{
			OldShared.Add(Name);
		}
	}
	for (const FName& Name : NewSide.RowOrder)
	{
		if (OldNames.Contains(Name))
		{
			NewShared.Add(Name);
		}
	}
	if (OldShared != NewShared)
	{
		const FText Label = LOCTEXT("OrderChanged", "Row order changed");
		Result.DiffRoots.Add(MakeEntry(Label, Label, OrderColor, NAME_None, NAME_None, JoinRowOrder(OldShared), JoinRowOrder(NewShared), true));
	}

	Result.DiffRoots.Append(MoveTemp(RowEntries));
	if (Result.DiffRoots.Num() == 0)
	{
		const FText Label = LOCTEXT("NoDifferences", "No differences detected...");
		Result.DiffRoots.Add(MakeEntry(Label, Label, NeutralColor, NAME_None, NAME_None, FString(), FString(), false));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
