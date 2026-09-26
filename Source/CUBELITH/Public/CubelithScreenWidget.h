// 画面（UMG）の共通の基底クラスと、仮の画面を C++ だけで組む仕組み（RULES.md 6 章・Docs/SPEC_UE.md 4 章）。
// 移植元は Web 版の WebMock/src/ui/screens.ts（画面の生成と破棄を対にする）と widgets.ts（ボタンの作り方）。
//
// 画面のレイアウトの `.uasset`（ウィジェットブループリント）は人が作る（Docs/SPEC_UE.md 0 章）ので、
// **振る舞い（どのボタンで何が起きるか・どの値を出すか）はこの基底クラスと派生クラスの C++ に持ち、
// 見た目は仮のものを C++ だけで組む**。人が同じ基底クラスから派生したウィジェットブループリントを作り、
// 同じ名前の部品を置けば、C++ の組み立ては働かず人のレイアウトがそのまま使われる（手順は Docs/SPEC_UE.md 4 章）。
//
// 解釈: 「人がレイアウトを作ったか」は `WidgetTree->RootWidget` があるかで見る。ウィジェットブループリントから
// 作ったウィジェットは必ず根の部品を持ち、C++ のクラスから直接作ったウィジェットは必ず持たない
// （UUserWidget::Initialize が空の WidgetTree を作るだけ）ので、これが「バインドが無い = 仮の画面」の判定になる。
// 部品ごとのバインド（BindWidgetOptional）は埋まっていなければ触らないだけにしてあるので、人のレイアウトに
// 一部の部品が無くても落ちない。

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Templates/Function.h"

#include "CubelithScreenWidget.generated.h"

class UButton;
class UHorizontalBox;
class UPanelWidget;
class UTextBlock;
class UVerticalBox;
class UWidget;

namespace Cubelith
{
	/**
	 * 指で押せるボタンの最小の辺の長さ（RULES.md 6 章の「44 px 以上」）。
	 * UMG の長さは端末非依存の単位（DPI スケールが掛かる前）なので、そのまま 44 を使う
	 */
	inline constexpr float MinTouchTargetPx = 44.0f;
}

/**
 * ボタンを押したときの処理の受け皿。
 *
 * UButton::OnClicked は動的デリゲート（UFUNCTION しか結べない）なのでラムダを直接結べない。
 * そこで「UFUNCTION 1 個とラムダ 1 個を持つ小さな UObject」を挟み、値ごとに違う処理
 * （N = 3..7 のボタンなど）をラムダで書けるようにする。寿命は UCubelithScreenWidget が握る
 */
UCLASS()
class CUBELITH_API UCubelithButtonAction : public UObject
{
	GENERATED_BODY()

public:
	/** 押したときに呼ぶ処理。TFunction は UPROPERTY にできないので、この UObject 自体を持ち主が握って寿命を保つ */
	TFunction<void()> Action;

	UFUNCTION()
	void HandleClicked();
};

/**
 * 画面 1 つ分の基底クラス（タイトル / プレイ中の HUD / クリア）。
 *
 * 派生クラスは BuildFallbackLayout で仮のレイアウトを組み、BindBehavior で部品に振る舞いを付ける。
 * どちらも「人のレイアウトがある場合」と「仮の画面の場合」の両方で通るように書く
 * （BindBehavior は仮の画面でも呼ばれるので、振る舞いの記述は 1 箇所で済む）。
 *
 * 生成と AddToViewport / RemoveFromParent は ACubelithGameMode がまとめて受け持つ（Docs/SPEC_UE.md 4 章）。
 */
UCLASS(Abstract)
class CUBELITH_API UCubelithScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * レイアウトの組み立てと振る舞いの結び付けを済ませる（**何度呼んでも 1 度しか走らない**）。
	 *
	 * 普段はエンジンが NativeOnInitialized（CreateWidget の中）か RebuildWidget（AddToViewport の経路）から
	 * 通すので、画面を出す側が呼ぶ必要は無い。Automation Test が Slate の実体を作らずに
	 * 部品の木と振る舞いを確かめるための入口としても使う
	 */
	void PrepareLayout();

protected:
	virtual void NativeOnInitialized() override;

	/**
	 * Slate の実体を作り直すときに呼ばれる（AddToViewport の経路）。ここでも組み立てを通すのは、
	 * NativeOnInitialized が呼ばれるのは PlayerContext があるときだけで、
	 * 取りこぼしたときに空の画面が出るのを避けるため（組み立ては 1 度しか走らない）
	 */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/**
	 * 仮のレイアウトを C++ で組む（人のレイアウトが無いときだけ呼ばれる）。
	 * 派生クラスは ConstructCenteredPanelRoot などのヘルパで組み立て、部品を自分の
	 * BindWidgetOptional のメンバへ入れる（そうすると BindBehavior が両方の場合で同じコードになる）
	 */
	virtual void BuildFallbackLayout() {}

	/** 部品に振る舞い（押したときの処理・初期の表示）を付ける。人のレイアウトでも仮の画面でも呼ばれる */
	virtual void BindBehavior() {}

	/** 人が UMG でレイアウトを作っているか（上の「解釈:」のとおり WidgetTree->RootWidget の有無で見る） */
	bool HasAuthoredLayout() const;

	/**
	 * 中央寄せのパネル（Overlay → Border → VerticalBox）を作って画面の根にし、中身を並べる縦箱を返す。
	 * 縦持ちの画面を前提に、背景は文字が読める程度に暗くするだけ（色の指定は最小限にする。人が UMG で作り直す）
	 */
	UVerticalBox* ConstructCenteredPanelRoot();

	/** 文字を 1 行足す。FontSize は端末非依存の大きさ（既定のフォントをそのまま使う） */
	UTextBlock* ConstructText(UPanelWidget* Parent, const FText& Text, int32 FontSize);

	/** 選択肢を横に並べる箱を 1 つ足す */
	UHorizontalBox* ConstructRow(UPanelWidget* Parent);

	/**
	 * ボタンを 1 つ足す（ラベル・指で押せる最小の大きさ・押したときの処理）。
	 * 押せる大きさを確保するため USizeBox で包んで Parent に入れ、返すのは中の UButton。
	 * 以後の画面（HUD・クリア画面）でも同じヘルパを使う
	 */
	UButton* ConstructButton(UPanelWidget* Parent, const FText& Label, TFunction<void()> OnClicked,
		float MinWidthPx = Cubelith::MinTouchTargetPx);

	/** 既にあるボタン（人が UMG で置いたもの / 上で組んだもの）に押したときの処理を結ぶ */
	void BindButton(UButton* Button, TFunction<void()> OnClicked);

	/** ボタンの選択状態を色で見せる（仮の見せ方。人が UMG で作るときはボタンのスタイルで表す） */
	static void SetButtonSelected(UButton* Button, bool bSelected);

	/** 文字の中身を差し替える（部品が無ければ何もしない）。BindWidgetOptional の取りこぼしを毎回書かないため */
	static void SetTextSafe(UTextBlock* TextBlock, const FText& Text);

private:
	/** PrepareLayout を通したか */
	bool bLayoutReady = false;

	/**
	 * 人が UMG でレイアウトを作っているか（PrepareLayout が組み立てる前に見て覚える）。
	 * 自分で組むと WidgetTree->RootWidget が埋まって区別できなくなるので、判定は 1 度だけ行う
	 */
	bool bAuthoredLayout = false;

	/** ボタンを押したときの処理の持ち主（GC に回収されないようここで握る） */
	UPROPERTY()
	TArray<TObjectPtr<UCubelithButtonAction>> ButtonActions;
};
