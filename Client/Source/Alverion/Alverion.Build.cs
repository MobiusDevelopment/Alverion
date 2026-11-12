using UnrealBuildTool;

public class Alverion : ModuleRules
{
	public Alverion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "CommonUI", "CommonInput", "GameplayTags" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "Network", "ApplicationCore", "RHI", "HairStrandsCore", "MoviePlayer", "AnimationCore", "RigLogicModule", "RigLogicLib", "ClothingSystemRuntimeCommon", "ClothingSystemRuntimeInterface", "ChaosCloth", "Chaos", "ChaosClothAssetEngine", "Json" });
	}
}
