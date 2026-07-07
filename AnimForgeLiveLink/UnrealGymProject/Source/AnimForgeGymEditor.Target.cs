// AnimForgeGymEditor.Target.cs

using UnrealBuildTool;

public class AnimForgeGymEditorTarget : TargetRules
{
	public AnimForgeGymEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AnimForgeGym");
	}
}
