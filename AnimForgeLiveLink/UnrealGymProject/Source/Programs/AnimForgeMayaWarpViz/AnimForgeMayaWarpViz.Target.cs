// AnimForgeMayaWarpViz.Target.cs
//
// Program target that produces the Maya plugin DLL. UBT emits
// AnimForgeMayaWarpViz.dll; the post-build step in BuildMayaPlugin.bat renames
// it to AnimForgeMayaWarpViz.mll (Maya loads .mll, which is just a DLL).

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class AnimForgeMayaWarpVizTarget : TargetRules
{
	public AnimForgeMayaWarpVizTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "AnimForgeMayaWarpViz";

		// Build a DLL, not an executable, and keep the engine out of it.
		bShouldCompileAsDLL = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = false;
		bBuildDeveloperTools = false;
		bCompileICU = false;
		bUsesSlate = false;
		bIsBuildingConsoleApplication = true;
		bLegalToDistributeBinary = true;

		// The module has no UE dependencies; skip default engine includes.
		bUseSharedPCHs = false;
		bHasExports = true; // initializePlugin / uninitializePlugin
	}
}
