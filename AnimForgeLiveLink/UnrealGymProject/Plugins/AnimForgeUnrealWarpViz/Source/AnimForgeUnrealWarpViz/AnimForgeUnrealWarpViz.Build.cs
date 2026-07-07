// AnimForgeUnrealWarpViz.Build.cs

using System.IO;
using UnrealBuildTool;

public class AnimForgeUnrealWarpViz : ModuleRules
{
	public AnimForgeUnrealWarpViz(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Shared protocol / math with the Maya plugin (compiled via
		// Private/WarpVizSharedImpl.cpp). The shared code is exception-free
		// plain C++, so no build settings need to change.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Shared", "AnimForgeWarpVizShared"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Sockets",
			"Networking",
			"DeveloperSettings",
			"MotionWarping",
			"AnimationBlueprintLibrary", // AnimPose sampling for ghost poses
			"UnrealEd",
		});
	}
}
