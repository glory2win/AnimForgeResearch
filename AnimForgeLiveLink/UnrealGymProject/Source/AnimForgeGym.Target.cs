// AnimForgeGym.Target.cs

using UnrealBuildTool;

public class AnimForgeGymTarget : TargetRules
{
	public AnimForgeGymTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AnimForgeGym");
	}
}
