#include "ContentBrowserMenuContexts.h"
#include "Engine/DataTable.h"
#include "Modules/ModuleManager.h"
#include "STableDiffRevisionMenu.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "UnrealTableDff"

class FUnrealTableDffModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealTableDffModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UDataTable::StaticClass());
		if (Menu == nullptr)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		Section.AddDynamicEntry("TableDiff", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UDataTable* Table = nullptr;
			if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection))
			{
				if (Context->SelectedAssets.Num() == 1)
				{
					const TArray<UDataTable*> Tables = Context->LoadSelectedObjects<UDataTable>();
					if (Tables.Num() == 1)
					{
						Table = Tables[0];
					}
				}
			}

			TWeakObjectPtr<UDataTable> WeakTable = Table;
			InSection.AddSubMenu(
				"TableDiff",
				LOCTEXT("TableDiff", "TableDiff"),
				LOCTEXT("TableDiffTooltip", "Diff this Data Table against a Perforce or Git history revision."),
				FNewToolMenuDelegate::CreateLambda([WeakTable](UToolMenu* SubMenu)
				{
					if (SubMenu == nullptr)
					{
						return;
					}

					FToolMenuSection& RevisionSection = SubMenu->AddSection("TableDiffRevisions");
					RevisionSection.AddEntry(FToolMenuEntry::InitWidget(
						"TableDiffRevisionList",
						SNew(STableDiffRevisionMenu, WeakTable.Get()),
						FText::GetEmpty(),
						true,
						false,
						true));
				}),
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Diff"));
		}));
	}
};

IMPLEMENT_MODULE(FUnrealTableDffModule, UnrealTableDff)

#undef LOCTEXT_NAMESPACE
