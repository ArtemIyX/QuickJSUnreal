// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class QuickJSUnreal : ModuleRules
{
	public QuickJSUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string quickJsRoot = Path.Combine(ModuleDirectory, "..", "ThirdParty", "QuickJS");
			string importLibrary = Path.Combine(quickJsRoot, "lib", "Win64", "qjs-import.lib");
			string runtimeLibrary = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "QuickJS", "Win64", "qjs.dll");
			if (!File.Exists(importLibrary) || !File.Exists(runtimeLibrary))
			{
				throw new BuildException("QuickJSUnreal requires the matching shared QuickJS-NG import library and DLL. Run BuildScripts/BuildQuickJSWin64.ps1 as described in the plugin README.");
			}

			PublicSystemIncludePaths.Add(Path.Combine(quickJsRoot, "include"));
			PublicDefinitions.Add("USING_QJS_SHARED=1");
			PublicAdditionalLibraries.Add(importLibrary);
			PublicDelayLoadDLLs.Add("qjs.dll");
			RuntimeDependencies.Add(runtimeLibrary);
		}

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Projects",
				"Engine",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
