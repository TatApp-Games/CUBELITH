// 画面の共通の基底クラスと、仮の画面を C++ だけで組む仕組み（RULES.md 6 章・Docs/SPEC_UE.md 4 章）。
// 移植元は WebMock/src/ui/widgets.ts（createButton / createOptionGroup / setSelected）で、
// DOM と CSS の代わりに UWidgetTree::ConstructWidget で UMG の部品を組む。

#include "CubelithScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Layout/Margin.h"

namespace
{
	/**
	 * 仮の画面の色（RULES.md 6 章には色の決まりが無い。人が UMG で作り直す前提なので、
	 * 「文字が読める」「押せる」「選んでいるものが分かる」に足りる最小限だけを決める）
	 */
	const FLinearColor PanelBackgroundColor(0.02f, 0.03f, 0.05f, 0.82f);
	const FLinearColor ButtonColorNormal(0.30f, 0.32f, 0.36f, 1.0f);
	const FLinearColor ButtonColorSelected(0.20f, 0.62f, 1.00f, 1.0f);

	/** パネルの内側の余白と、縦に並べる部品の間隔 */
	constexpr float PanelPaddingPx = 24.0f;
	constexpr float StackSpacingPx = 6.0f;
}

void UCubelithButtonAction::HandleClicked()
{
	if (Action)
	{
		Action();
	}
}

void UCubelithScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	PrepareLayout();
}

TSharedRef<SWidget> UCubelithScreenWidget::RebuildWidget()
{
	// Slate の実体を作る前に組み立てを済ませる（UUserWidget::RebuildWidget は WidgetTree->RootWidget を見るので、
	// ここで根が入っていなければ空の画面になる）
	PrepareLayout();

	return Super::RebuildWidget();
}

void UCubelithScreenWidget::PrepareLayout()
{
	if (bLayoutReady || WidgetTree == nullptr)
	{
		return;
	}
	bLayoutReady = true;

	// 組み立てる前に見ておく（自分で組むと RootWidget が埋まってしまい、後から区別できなくなる）
	bAuthoredLayout = WidgetTree->RootWidget != nullptr;

	if (!bAuthoredLayout)
	{
		BuildFallbackLayout();
	}

	// 振る舞いは人のレイアウトでも仮の画面でも同じ（部品が無ければ触らない）
	BindBehavior();
}

bool UCubelithScreenWidget::HasAuthoredLayout() const
{
	return bAuthoredLayout;
}

UVerticalBox* UCubelithScreenWidget::ConstructCenteredPanelRoot()
{
	if (WidgetTree == nullptr)
	{
		return nullptr;
	}

	// Overlay を根にするのは、縦横の中央寄せを 1 つのスロットで指定できるため
	// （VerticalBox を根にすると画面の上端に貼り付く）
	UOverlay* const Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ScreenRoot"));
	UBorder* const Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ScreenPanel"));
	Panel->SetBrushColor(PanelBackgroundColor);
	Panel->SetPadding(FMargin(PanelPaddingPx));

	if (UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Root->AddChild(Panel)))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* const Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ScreenStack"));
	Panel->AddChild(Stack);

	WidgetTree->RootWidget = Root;
	return Stack;
}

UTextBlock* UCubelithScreenWidget::ConstructText(UPanelWidget* Parent, const FText& Text, int32 FontSize)
{
	if (WidgetTree == nullptr || Parent == nullptr)
	{
		return nullptr;
	}

	UTextBlock* const TextBlock = WidgetTree->ConstructWidget<UTextBlock>();
	TextBlock->SetText(Text);
	TextBlock->SetJustification(ETextJustify::Center);

	// フォントそのものは既定のまま（日本語は Slate の代替フォントで出る）。大きさだけ変える
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FontSize;
	TextBlock->SetFont(Font);

	if (UVerticalBoxSlot* const TextSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(TextBlock)))
	{
		TextSlot->SetPadding(FMargin(0.0f, StackSpacingPx * 0.5f));
		TextSlot->SetHorizontalAlignment(HAlign_Center);
	}
	return TextBlock;
}

UHorizontalBox* UCubelithScreenWidget::ConstructRow(UPanelWidget* Parent)
{
	if (WidgetTree == nullptr || Parent == nullptr)
	{
		return nullptr;
	}

	UHorizontalBox* const Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (UVerticalBoxSlot* const RowSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Row)))
	{
		RowSlot->SetPadding(FMargin(0.0f, StackSpacingPx * 0.5f));
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}
	return Row;
}

UButton* UCubelithScreenWidget::ConstructButton(UPanelWidget* Parent, const FText& Label,
	TFunction<void()> OnClicked, float MinWidthPx)
{
	if (WidgetTree == nullptr || Parent == nullptr)
	{
		return nullptr;
	}

	// 指で押せる大きさ（RULES.md 6 章の 44 px 以上）は USizeBox で確保する。
	// UButton 自体には最小の大きさを指定する口が無く、ラベルの余白で稼ぐと文字の大きさに引きずられる
	USizeBox* const SizeBox = WidgetTree->ConstructWidget<USizeBox>();
	SizeBox->SetMinDesiredHeight(Cubelith::MinTouchTargetPx);
	SizeBox->SetMinDesiredWidth(FMath::Max(MinWidthPx, Cubelith::MinTouchTargetPx));

	UButton* const Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(ButtonColorNormal);
	SizeBox->AddChild(Button);

	UTextBlock* const LabelText = WidgetTree->ConstructWidget<UTextBlock>();
	LabelText->SetText(Label);
	LabelText->SetJustification(ETextJustify::Center);
	if (UButtonSlot* const LabelSlot = Cast<UButtonSlot>(Button->AddChild(LabelText)))
	{
		LabelSlot->SetPadding(FMargin(10.0f, 2.0f));
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UPanelSlot* const AddedSlot = Parent->AddChild(SizeBox);
	if (UHorizontalBoxSlot* const RowSlot = Cast<UHorizontalBoxSlot>(AddedSlot))
	{
		// 選択肢の行（横並び）に入れたとき
		RowSlot->SetPadding(FMargin(StackSpacingPx * 0.5f, 0.0f));
		RowSlot->SetVerticalAlignment(VAlign_Center);
	}
	else if (UVerticalBoxSlot* const StackSlot = Cast<UVerticalBoxSlot>(AddedSlot))
	{
		// 縦並びに直接入れたとき（「開始」など 1 個だけのボタン）
		StackSlot->SetPadding(FMargin(0.0f, StackSpacingPx));
		StackSlot->SetHorizontalAlignment(HAlign_Center);
	}

	BindButton(Button, MoveTemp(OnClicked));
	return Button;
}

void UCubelithScreenWidget::BindButton(UButton* Button, TFunction<void()> OnClicked)
{
	if (Button == nullptr || !OnClicked)
	{
		return;
	}

	UCubelithButtonAction* const ActionObject = NewObject<UCubelithButtonAction>(this);
	ActionObject->Action = MoveTemp(OnClicked);
	// ラムダを持つ UObject は誰かが握っていないと回収されるので、画面の寿命に合わせてここで持つ
	ButtonActions.Add(ActionObject);

	Button->OnClicked.AddDynamic(ActionObject, &UCubelithButtonAction::HandleClicked);
}

void UCubelithScreenWidget::SetButtonSelected(UButton* Button, bool bSelected)
{
	if (Button == nullptr)
	{
		return;
	}

	// 仮の見せ方なので色だけ（widgets.ts の setSelected が class を付け替えるのと同じ役目）
	Button->SetBackgroundColor(bSelected ? ButtonColorSelected : ButtonColorNormal);
}

void UCubelithScreenWidget::SetTextSafe(UTextBlock* TextBlock, const FText& Text)
{
	if (TextBlock != nullptr)
	{
		TextBlock->SetText(Text);
	}
}
