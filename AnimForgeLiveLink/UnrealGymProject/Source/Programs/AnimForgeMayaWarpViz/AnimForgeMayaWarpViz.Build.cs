// AnimForgeMayaWarpViz.Build.cs
//
// Builds the Maya plugin (.mll) with UnrealBuildTool, following the pattern of
// Epic's MayaLiveLinkPlugin. Requires the Maya devkit; point MAYA_SDK (or
// MAYA_LOCATION for a full install) at it before building:
//
//   set MAYA_SDK=C:\devkits\Maya2025\devkitBase
//   Engine\Build\BatchFiles\RunUBT.bat AnimForgeMayaWarpViz Win64 Development

using System;
using System.IO;
using UnrealBuildTool;

public class AnimForgeMayaWarpViz : ModuleRules
{
	public AnimForgeMayaWarpViz(ReadOnlyTargetRules Target) : base(Target)
	{
		// Maya's SDK is exception-based and this module never touches UE
		// runtime types, so keep the module as plain C++ as possible.
		PCHUsage = PCHUsageMode.NoPCHs;
		bEnableExceptions = true;
		bUseRTTI = true;
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp17;

		// No UE module dependencies: the .mll must not drag engine DLLs into Maya.
		PublicDependencyModuleNames.Clear();
		PrivateDependencyModuleNames.Clear();

		// Shared protocol / math (compiled via Private/SharedImpl.cpp).
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "..", "Shared", "AnimForgeWarpVizShared"));

		// --- Maya devkit ---------------------------------------------------
		string MayaSdk = Environment.GetEnvironmentVariable("MAYA_SDK");
		if (string.IsNullOrEmpty(MayaSdk))
		{
			MayaSdk = Environment.GetEnvironmentVariable("MAYA_LOCATION");
		}
		if (string.IsNullOrEmpty(MayaSdk))
		{
			throw new BuildException(
				"AnimForgeMayaWarpViz: set MAYA_SDK to the Maya devkit root " +
				"(the folder containing include/ and lib/).");
		}

		PublicSystemIncludePaths.Add(Path.Combine(MayaSdk, "include"));

		string MayaLibDir = Path.Combine(MayaSdk, "lib");
		PublicAdditionalLibraries.Add(Path.Combine(MayaLibDir, "OpenMaya.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(MayaLibDir, "OpenMayaAnim.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(MayaLibDir, "Foundation.lib"));

		// Winsock for the gym connection.
		PublicSystemLibraries.Add("Ws2_32.lib");

		PublicDefinitions.Add("NT_PLUGIN");
		PublicDefinitions.Add("REQUIRE_IOSTREAM");
	}
}
