#pragma once

#include "ISourceControlOperation.h"
#include "ISourceControlProvider.h"
#include "Widgets/SCompoundWidget.h"

class FUpdateStatus;
class SVerticalBox;
class UDataTable;

class STableDiffRevisionMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STableDiffRevisionMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDataTable* InTable);
	virtual ~STableDiffRevisionMenu() override;

private:
	EVisibility GetInProgressVisibility() const;
	EVisibility GetCancelButtonVisibility() const;
	FReply OnCancelButtonClicked() const;
	void OnSourceControlQueryComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);
	void PopulateRevisionList(ECommandResult::Type InResult);

	enum class EQueryState : uint8
	{
		Idle,
		InProgress,
		Complete
	};

	TWeakObjectPtr<UDataTable> Table;
	FString Filename;
	TSharedPtr<SVerticalBox> MenuBox;
	TSharedPtr<FUpdateStatus, ESPMode::ThreadSafe> SourceControlQueryOp;
	EQueryState QueryState = EQueryState::Idle;
};
