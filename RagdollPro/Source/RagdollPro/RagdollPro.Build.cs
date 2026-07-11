// Copyright AnimForge Studios. All Rights Reserved.

using UnrealBuildTool;

public class RagdollPro : ModuleRules
{
	public RagdollPro(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"PhysicsCore"
		});
	}
}
