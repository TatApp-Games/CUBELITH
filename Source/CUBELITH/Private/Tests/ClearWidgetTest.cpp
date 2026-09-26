// クリア画面（UCubelithClearWidget。RULES.md 2 章 5 / 6 章・Docs/SPEC_UE.md 4 章の「クリア画面」）のテスト。
// 移植元 WebMock/src/ui/clearScreen.ts に対応するテストは WebMock にも無いので、確かめるのは
// 仮のレイアウトに「もう一度」「難易度を変える」が組まれること・遊んだ盤面の条件とシードが出ること・
// 押すとそれぞれのデリゲートが 1 回だけ呼ばれること。
//
// 作りは TitleWidgetTest.cpp と同じ（Slate の実体は作らず、UMG の部品の木と振る舞いだけを見る）。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UObject/Package.h"

#include "CubelithClearWidget.h"

namespace CubelithRenderTests
{
	// ClearWidgetTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace ClearWidgetTestDetail
	{
		/** 仮のレイアウトを組んだクリア画面を 1 つ作る（エンジンが CreateWidget で通すのと同じ順で呼ぶ） */
		UCubelithClearWidget* MakeClearWidget(int32 N, int32 M, bool bAllowRotation, uint32 Seed)
		{
			UCubelithClearWidget* const Widget = NewObject<UCubelithClearWidget>(GetTransientPackage());
			// テストの間に GC で消えないよう根に付ける（終わりに ReleaseClearWidget で外す）
			Widget->AddToRoot();
			// UWidgetTree を作る（PlayerContext が無いので NativeOnInitialized は呼ばれない）
			Widget->Initialize();
			Widget->PrepareLayout();
			Widget->SetBoardSummary(N, M, bAllowRotation, Seed);
			return Widget;
		}

		void ReleaseClearWidget(UCubelithClearWidget* Widget)
		{
			if (Widget != nullptr)
			{
				Widget->RemoveFromRoot();
			}
		}

		/** ボタンのラベル（中の UTextBlock の文字）。無ければ空文字 */
		FString ButtonLabel(UButton* Button)
		{
			if (Button == nullptr)
			{
				return FString();
			}
			const UTextBlock* const Label = Cast<UTextBlock>(Button->GetChildAt(0));
			return (Label != nullptr) ? Label->GetText().ToString() : FString();
		}

		/** ラベルが一致するボタン。無い / 複数あれば nullptr */
		UButton* FindButton(const UCubelithClearWidget& Widget, const FString& Label)
		{
			UButton* Found = nullptr;
			if (Widget.WidgetTree == nullptr)
			{
				return nullptr;
			}

			Widget.WidgetTree->ForEachWidget([&Found, &Label](UWidget* Child)
			{
				UButton* const Button = Cast<UButton>(Child);
				if (Button != nullptr && ButtonLabel(Button) == Label)
				{
					Found = (Found == nullptr) ? Button : nullptr;
				}
			});
			return Found;
		}

		/** ボタンを押す（Slate を通さず、押されたときの動的デリゲートを直に鳴らす） */
		bool ClickButton(const UCubelithClearWidget& Widget, const FString& Label)
		{
			UButton* const Button = FindButton(Widget, Label);
			if (Button == nullptr)
			{
				return false;
			}
			Button->OnClicked.Broadcast();
			return true;
		}

		/** 画面の中の文字をぜんぶ集める（ボタンのラベルも入る） */
		TArray<FString> CollectTexts(const UCubelithClearWidget& Widget)
		{
			TArray<FString> Texts;
			if (Widget.WidgetTree != nullptr)
			{
				Widget.WidgetTree->ForEachWidget([&Texts](UWidget* Child)
				{
					if (const UTextBlock* const TextBlock = Cast<UTextBlock>(Child))
					{
						Texts.Add(TextBlock->GetText().ToString());
					}
				});
			}
			return Texts;
		}

		bool HasText(const UCubelithClearWidget& Widget, const FString& Expected)
		{
			return CollectTexts(Widget).Contains(Expected);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithClearWidgetFallbackLayoutTest, "CUBELITH.Render.ClearWidget.FallbackLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithClearWidgetFallbackLayoutTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ClearWidgetTestDetail;

	// 人の UMG が無い（C++ のクラスから直に作った）ので、仮のレイアウトが組まれる
	UCubelithClearWidget* const Widget = MakeClearWidget(4, 8, /*bAllowRotation=*/true, 4242);

	TestNotNull(TEXT("WidgetTree"), Widget->WidgetTree.Get());
	TestNotNull(TEXT("仮のレイアウトの根"), Widget->WidgetTree->RootWidget.Get());

	// ボタンは RULES.md 6 章の 2 つだけ（演出のスキップなどは足さない）
	TestNotNull(TEXT("「もう一度」のボタン"), FindButton(*Widget, TEXT("もう一度")));
	TestNotNull(TEXT("「難易度を変える」のボタン"), FindButton(*Widget, TEXT("難易度を変える")));

	// 見出しと、遊んだ盤面の条件・シード（clearScreen.ts に倣う。回転は UE 版で足した「解釈:」）
	TestTrue(TEXT("クリアの見出し"), HasText(*Widget, TEXT("CLEAR")));
	TestTrue(TEXT("遊んだ盤面の条件"), HasText(*Widget, TEXT("N = 4 / M = 8 / 回転あり を組み上げた")));
	TestTrue(TEXT("シードの行"), HasText(*Widget, TEXT("seed 4242")));

	ReleaseClearWidget(Widget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithClearWidgetActionsTest, "CUBELITH.Render.ClearWidget.Actions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithClearWidgetActionsTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::ClearWidgetTestDetail;

	UCubelithClearWidget* const Widget = MakeClearWidget(3, 4, /*bAllowRotation=*/false, 7);

	// 回転「なし」の盤面のまとめの行（タイトルと同じ文言）
	TestTrue(TEXT("回転なしの条件"), HasText(*Widget, TEXT("N = 3 / M = 4 / 回転なし を組み上げた")));

	int32 RetryCount = 0;
	int32 BackToTitleCount = 0;
	Widget->OnRetry.BindLambda([&RetryCount]() { ++RetryCount; });
	Widget->OnBackToTitle.BindLambda([&BackToTitleCount]() { ++BackToTitleCount; });

	// 押す前は何も起きない
	TestEqual(TEXT("押す前の「もう一度」"), RetryCount, 0);
	TestEqual(TEXT("押す前の「難易度を変える」"), BackToTitleCount, 0);

	TestTrue(TEXT("「もう一度」を押せた"), ClickButton(*Widget, TEXT("もう一度")));
	TestEqual(TEXT("「もう一度」で 1 回だけ呼ばれる"), RetryCount, 1);
	TestEqual(TEXT("「もう一度」で「難易度を変える」は呼ばれない"), BackToTitleCount, 0);

	TestTrue(TEXT("「難易度を変える」を押せた"), ClickButton(*Widget, TEXT("難易度を変える")));
	TestEqual(TEXT("「難易度を変える」で 1 回だけ呼ばれる"), BackToTitleCount, 1);
	TestEqual(TEXT("「難易度を変える」で「もう一度」は呼ばれない"), RetryCount, 1);

	ReleaseClearWidget(Widget);
	return true;
}

#endif
