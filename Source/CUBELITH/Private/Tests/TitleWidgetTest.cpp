// タイトル / 難易度選択画面（UCubelithTitleWidget。RULES.md 6 章・Docs/SPEC_UE.md 4 章の「画面（UI）」）のテスト。
// 移植元 WebMock/src/ui/titleScreen.ts に対応するテストは WebMock にも無いので、確かめるのは
// 仮のレイアウトが組まれること・N を変えると M のプリセットが作り直されて選択が引き継がれること・
// 「開始」がタイトルで選ばれている値を渡すこと・「続きから」とクリア回数（RULES.md 3.8 / 6 章）の出し方。
//
// Slate の実体（SButton など）は作らず、UMG の部品の木（UWidgetTree）と振る舞いだけを見る。
// UUserWidget::Initialize は UWorld を要らないので -nullrhi のコマンドラインでも走り、
// ボタンを押すのは UButton::OnClicked を直接 Broadcast して代える。

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UObject/Package.h"

#include "CubelithTitleWidget.h"

namespace CubelithRenderTests
{
	// TitleWidgetTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace TitleWidgetTestDetail
	{
		/** 仮のレイアウトを組んだタイトル画面を 1 つ作る（エンジンが CreateWidget で通すのと同じ順で呼ぶ） */
		UCubelithTitleWidget* MakeTitleWidget(int32 N, int32 M, bool bAllowRotation, uint32 Seed)
		{
			UCubelithTitleWidget* const Widget = NewObject<UCubelithTitleWidget>(GetTransientPackage());
			// テストの間に GC で消えないよう根に付ける（終わりに ReleaseTitleWidget で外す）
			Widget->AddToRoot();
			// UWidgetTree を作る（PlayerContext が無いので NativeOnInitialized は呼ばれない）
			Widget->Initialize();
			Widget->PrepareLayout();
			Widget->SetInitialSelection(N, M, bAllowRotation, Seed);
			return Widget;
		}

		void ReleaseTitleWidget(UCubelithTitleWidget* Widget)
		{
			if (Widget != nullptr)
			{
				Widget->RemoveFromRoot();
			}
		}

		/** 画面の中のボタンをぜんぶ集める */
		TArray<UButton*> CollectButtons(const UCubelithTitleWidget& Widget)
		{
			TArray<UButton*> Buttons;
			if (Widget.WidgetTree != nullptr)
			{
				Widget.WidgetTree->ForEachWidget([&Buttons](UWidget* Child)
				{
					if (UButton* const Button = Cast<UButton>(Child))
					{
						Buttons.Add(Button);
					}
				});
			}
			return Buttons;
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

		/** ラベルが一致するボタンの数（N と M で同じ数字が出ることがあるので、数で確かめる） */
		int32 CountButtons(const UCubelithTitleWidget& Widget, const FString& Label)
		{
			int32 Count = 0;
			for (UButton* const Button : CollectButtons(Widget))
			{
				if (ButtonLabel(Button) == Label)
				{
					++Count;
				}
			}
			return Count;
		}

		/** ラベルが一致するボタン。無い / 複数あれば nullptr（呼ぶ側は一意になるラベルを選ぶ） */
		UButton* FindButton(const UCubelithTitleWidget& Widget, const FString& Label)
		{
			UButton* Found = nullptr;
			for (UButton* const Button : CollectButtons(Widget))
			{
				if (ButtonLabel(Button) == Label)
				{
					if (Found != nullptr)
					{
						return nullptr;
					}
					Found = Button;
				}
			}
			return Found;
		}

		/** ボタンを押す（Slate を通さず、押されたときの動的デリゲートを直に鳴らす） */
		bool ClickButton(const UCubelithTitleWidget& Widget, const FString& Label)
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
		TArray<FString> CollectTexts(const UCubelithTitleWidget& Widget)
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

		bool HasText(const UCubelithTitleWidget& Widget, const FString& Expected)
		{
			return CollectTexts(Widget).Contains(Expected);
		}

		/**
		 * ボタンが見えているか。仮の画面のボタンは USizeBox で包んであり、隠すときは入れ物ごと
		 * Collapsed になる（UCubelithScreenWidget::SetButtonVisible）ので、親までさかのぼって見る
		 */
		bool IsButtonVisible(const UCubelithTitleWidget& Widget, const FString& Label)
		{
			const UWidget* Current = FindButton(Widget, Label);
			while (Current != nullptr)
			{
				if (Current->GetVisibility() == ESlateVisibility::Collapsed
					|| Current->GetVisibility() == ESlateVisibility::Hidden)
				{
					return false;
				}
				Current = Current->GetParent();
			}
			return true;
		}

		/** クリア回数を 1 つ作る（難易度ごとのキーは Cubelith::DifficultyKey の形） */
		FCubelithSavedClears MakeClears(int32 Total, const FString& Key, int32 Count)
		{
			FCubelithSavedClears Clears;
			Clears.Total = Total;
			Clears.ByDifficulty.Add(Key, Count);
			return Clears;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleWidgetFallbackLayoutTest, "CUBELITH.Render.TitleWidget.FallbackLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleWidgetFallbackLayoutTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::TitleWidgetTestDetail;

	// 人の UMG が無い（C++ のクラスから直に作った）ので、仮のレイアウトが組まれる
	UCubelithTitleWidget* const Widget = MakeTitleWidget(4, 8, /*bAllowRotation=*/true, 42);

	TestNotNull(TEXT("WidgetTree"), Widget->WidgetTree.Get());
	TestNotNull(TEXT("仮のレイアウトの根"), Widget->WidgetTree->RootWidget.Get());

	// N が 5 個（3..7）・M が N=4 のプリセット 5 個・パズルの回転 2 個・「続きから」1 個・「開始」1 個。
	// 「続きから」は途中の盤面が無くても作り、隠すだけ（人の UMG と同じ経路にするため）
	TestEqual(TEXT("ボタンの数"), CollectButtons(*Widget).Num(), 14);

	// N は 3..7 の 5 個、M は N=4 のプリセット 4 / 6 / 8 / 10 / 12。4 と 6 は両方に出るので 2 個になる
	TestEqual(TEXT("ラベル 3 のボタン（N=3）"), CountButtons(*Widget, TEXT("3")), 1);
	TestEqual(TEXT("ラベル 4 のボタン（N=4 と M=4）"), CountButtons(*Widget, TEXT("4")), 2);
	TestEqual(TEXT("ラベル 5 のボタン（N=5）"), CountButtons(*Widget, TEXT("5")), 1);
	TestEqual(TEXT("ラベル 6 のボタン（N=6 と M=6）"), CountButtons(*Widget, TEXT("6")), 2);
	TestEqual(TEXT("ラベル 7 のボタン（N=7）"), CountButtons(*Widget, TEXT("7")), 1);
	TestEqual(TEXT("ラベル 8 のボタン（M=8）"), CountButtons(*Widget, TEXT("8")), 1);
	TestEqual(TEXT("ラベル 10 のボタン（M=10）"), CountButtons(*Widget, TEXT("10")), 1);
	TestEqual(TEXT("ラベル 12 のボタン（M=12）"), CountButtons(*Widget, TEXT("12")), 1);
	TestEqual(TEXT("N=5 のプリセット（11）は出ていない"), CountButtons(*Widget, TEXT("11")), 0);

	TestNotNull(TEXT("「開始」のボタン"), FindButton(*Widget, TEXT("開始")));
	TestNotNull(TEXT("「なし」のボタン"), FindButton(*Widget, TEXT("なし")));
	TestNotNull(TEXT("「あり」のボタン"), FindButton(*Widget, TEXT("あり")));

	// まとめの行とシードの表示（RULES.md 6 章の「シードの表示（小さく）」）
	TestTrue(TEXT("まとめの行"), HasText(*Widget, TEXT("N = 4 / M = 8 / 回転あり（64 ボクセルを 8 個に分割）")));
	TestTrue(TEXT("シードの行"), HasText(*Widget, TEXT("seed 42")));

	// 途中の盤面を渡していないので「続きから」は隠れている（RULES.md 6 章の「無ければ出さない」）
	TestNotNull(TEXT("「続きから」のボタン"), FindButton(*Widget, TEXT("続きから")));
	TestFalse(TEXT("「続きから」は出ていない"), IsButtonVisible(*Widget, TEXT("続きから")));

	// クリア回数の行は途中の盤面が無くても出る（RULES.md 6 章。何も遊んでいなければ 0 回）
	TestTrue(TEXT("クリア回数の行"), HasText(*Widget, TEXT("クリア 合計 0 回 / この難易度 0 回")));

	ReleaseTitleWidget(Widget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleWidgetPresetSnapTest, "CUBELITH.Render.TitleWidget.PresetSnap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleWidgetPresetSnapTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::TitleWidgetTestDetail;

	// 初期選択の M がプリセットに無ければ最も近い段へ寄せる（N=5 は 5 / 8 / 11 / 14 / 17 で、9 → 8）
	UCubelithTitleWidget* const Widget = MakeTitleWidget(5, 9, /*bAllowRotation=*/false, 1);
	TestTrue(TEXT("プリセット外の M を寄せる"),
		HasText(*Widget, TEXT("N = 5 / M = 8 / 回転なし（125 ボクセルを 8 個に分割）")));

	// 範囲外の N は丸める（9 → 7。N=7 のプリセットは 7 / 12 / 17 / 22 / 27 で、8 → 7）
	UCubelithTitleWidget* const Clamped = MakeTitleWidget(9, 8, /*bAllowRotation=*/false, 2);
	TestTrue(TEXT("範囲外の N を丸める"),
		HasText(*Clamped, TEXT("N = 7 / M = 7 / 回転なし（343 ボクセルを 7 個に分割）")));

	ReleaseTitleWidget(Widget);
	ReleaseTitleWidget(Clamped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleWidgetSpaceSizeChangeTest, "CUBELITH.Render.TitleWidget.SpaceSizeChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleWidgetSpaceSizeChangeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::TitleWidgetTestDetail;

	// N=4（M のプリセットは 4 / 6 / 8 / 10 / 12）から始めるので、ラベル "5" は N のボタンだけにある
	UCubelithTitleWidget* const Widget = MakeTitleWidget(4, 8, /*bAllowRotation=*/false, 3);

	TestTrue(TEXT("N=5 を押せた"), ClickButton(*Widget, TEXT("5")));

	// M のプリセットが N=5 のもの（5 / 8 / 11 / 14 / 17）に作り直され、選択は 8 のまま引き継がれる
	TestTrue(TEXT("N を変えた後のまとめの行"),
		HasText(*Widget, TEXT("N = 5 / M = 8 / 回転なし（125 ボクセルを 8 個に分割）")));
	TestEqual(TEXT("M=11 のボタンができた"), CountButtons(*Widget, TEXT("11")), 1);
	TestEqual(TEXT("M=12 のボタンは消えた"), CountButtons(*Widget, TEXT("12")), 0);
	TestEqual(TEXT("ボタンの数は変わらない"), CollectButtons(*Widget).Num(), 14);

	// M=17 を選んでから N=3（プリセットは 3 / 4 / 5 / 6 / 7）へ落とすと、最も近い 7 へ寄る
	TestTrue(TEXT("M=17 を押せた"), ClickButton(*Widget, TEXT("17")));
	TestTrue(TEXT("N=3 を押せた"), ClickButton(*Widget, TEXT("3")));
	TestTrue(TEXT("プリセットが変わって選択が寄る"),
		HasText(*Widget, TEXT("N = 3 / M = 7 / 回転なし（27 ボクセルを 7 個に分割）")));

	ReleaseTitleWidget(Widget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleWidgetStartTest, "CUBELITH.Render.TitleWidget.Start",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleWidgetStartTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::TitleWidgetTestDetail;

	UCubelithTitleWidget* const Widget = MakeTitleWidget(4, 8, /*bAllowRotation=*/false, 12345);

	int32 StartCount = 0;
	FCubelithTitleSelection Received;
	Widget->OnStart.BindLambda([&StartCount, &Received](const FCubelithTitleSelection& Selection)
	{
		++StartCount;
		Received = Selection;
	});

	// 押す前は何も起きない
	TestEqual(TEXT("押す前"), StartCount, 0);

	TestTrue(TEXT("「あり」を押せた"), ClickButton(*Widget, TEXT("あり")));
	TestTrue(TEXT("M=10 を押せた"), ClickButton(*Widget, TEXT("10")));
	TestTrue(TEXT("「開始」を押せた"), ClickButton(*Widget, TEXT("開始")));

	TestEqual(TEXT("「開始」で 1 回だけ呼ばれる"), StartCount, 1);
	TestEqual(TEXT("渡された N"), Received.SpaceSize, 4);
	TestEqual(TEXT("渡された M"), Received.PieceCount, 10);
	TestTrue(TEXT("渡されたパズルの回転"), Received.bAllowRotation);
	TestEqual(TEXT("渡されたシード"), static_cast<int64>(Received.Seed), static_cast<int64>(12345));

	ReleaseTitleWidget(Widget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleWidgetResumeTest, "CUBELITH.Render.TitleWidget.Resume",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleWidgetResumeTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::TitleWidgetTestDetail;

	// タイトルで選んでいるのは N=4 / M=8 / 回転あり。途中の盤面は別の難易度（N=3 / M=5 / 回転なし）
	UCubelithTitleWidget* const Widget = MakeTitleWidget(4, 8, /*bAllowRotation=*/true, 7);

	Cubelith::FTitleResume Resume;
	Resume.SpaceSize = 3;
	Resume.PieceCount = 5;
	Resume.bAllowRotation = false;
	Resume.Remaining = 2;
	Widget->SetResume(TOptional<Cubelith::FTitleResume>(Resume));

	TestTrue(TEXT("「続きから」が出る"), IsButtonVisible(*Widget, TEXT("続きから")));
	// 出すのは**その盤面の**難易度で、タイトルで選んでいる N=4 / M=8 ではない（RULES.md 6 章）
	TestTrue(TEXT("途中の盤面の 1 行"),
		HasText(*Widget, TEXT("N = 3 / M = 5 / 回転なし・残り 2 ピース")));

	int32 ResumeCount = 0;
	Widget->OnResume.BindLambda([&ResumeCount]() { ++ResumeCount; });
	TestTrue(TEXT("「続きから」を押せた"), ClickButton(*Widget, TEXT("続きから")));
	TestEqual(TEXT("「続きから」で 1 回だけ呼ばれる"), ResumeCount, 1);

	// 途中の盤面を外すと隠れる（RULES.md 6 章の「無ければ出さない」）
	Widget->SetResume(TOptional<Cubelith::FTitleResume>());
	TestFalse(TEXT("外すと隠れる"), IsButtonVisible(*Widget, TEXT("続きから")));

	ReleaseTitleWidget(Widget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithTitleWidgetClearCountTest, "CUBELITH.Render.TitleWidget.ClearCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithTitleWidgetClearCountTest::RunTest(const FString& Parameters)
{
	using namespace CubelithRenderTests::TitleWidgetTestDetail;

	// 合計 5 回のうち N=4 / M=8 / 回転なし が 2 回（RULES.md 6 章の「合計」と「選んでいる難易度の回数」）
	UCubelithTitleWidget* const Widget = MakeTitleWidget(4, 8, /*bAllowRotation=*/false, 7);
	Widget->SetClearCounts(MakeClears(5, TEXT("4-8-0"), 2));

	TestTrue(TEXT("選んでいる難易度の回数が出る"),
		HasText(*Widget, TEXT("クリア 合計 5 回 / この難易度 2 回")));

	// 選択を変えると「この難易度」だけが変わる（記録の無い難易度は 0 回）
	TestTrue(TEXT("M=10 を押せた"), ClickButton(*Widget, TEXT("10")));
	TestTrue(TEXT("選択を変えると引き直す"),
		HasText(*Widget, TEXT("クリア 合計 5 回 / この難易度 0 回")));

	// 元の難易度へ戻すと元の回数に戻る
	TestTrue(TEXT("M=8 を押せた"), ClickButton(*Widget, TEXT("8")));
	TestTrue(TEXT("戻すと元の回数"),
		HasText(*Widget, TEXT("クリア 合計 5 回 / この難易度 2 回")));

	ReleaseTitleWidget(Widget);
	return true;
}

#endif
