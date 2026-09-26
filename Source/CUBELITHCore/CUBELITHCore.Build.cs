using UnrealBuildTool;

// ゲームロジック（RULES.md 3 章）。WebMock の src/core に対応し、描画に依存しない（Docs/SPEC_UE.md 7.1）
public class CUBELITHCore : ModuleRules
{
	public CUBELITHCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 依存してよいのは Core だけ。CoreUObject・Engine を足さない
		PublicDependencyModuleNames.Add("Core");

		// 照合データ（Docs/FIXTURES.md）を読むテストのためだけに使う。ロジックのコードからは使わない
		PrivateDependencyModuleNames.Add("Json");
	}
}
