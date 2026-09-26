// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CUBELITH : ModuleRules
{
	public CUBELITH(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// CUBELITHCore は Public に置く（Public/ のヘッダが FVec3 などを露出するため。Docs/SPEC_UE.md 7.1）
		// UMG も Public（Public/CubelithScreenWidget.h が UUserWidget を継承して露出するため。Docs/SPEC_UE.md 4 章の UI）
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "CUBELITHCore" });

		// 画面（UMG）の実装で Slate の型（FSlateFontInfo・FSlateBrush・FMargin）を直接触る
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
