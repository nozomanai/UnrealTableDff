using UnrealBuildTool;

public class UnrealTableDff : ModuleRules
{
	public UnrealTableDff(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"ToolMenus",
				"ContentBrowser",
				"SourceControl",
				"AssetDefinition",
				"InputCore"
			});
	}
}
