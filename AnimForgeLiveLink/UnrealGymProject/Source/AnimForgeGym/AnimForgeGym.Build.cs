// AnimForgeGym.Build.cs

using UnrealBuildTool;

public class AnimForgeGym : ModuleRules
{
	public AnimForgeGym(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});
	}
}
