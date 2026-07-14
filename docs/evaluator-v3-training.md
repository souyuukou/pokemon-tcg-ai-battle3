# V3評価器の固定デッキ学習

## 採用モデル

- デッキ: `majkel1337-85795098`
- ネイティブ自己対戦: 2,000対局（受理2,000、破棄0）
- ターンエンド葉: 63,495局面
- データSHA-256: `85985134eeb9070e90af073f830101a0114029d4e74c806df1cb105baddead57`
- モデルSHA-256: `3b5b4fe5078e91bee492552eab52df0ef94939576085c57d22740e5b2d3ca115`

各対局はseedと方策乱数を分離して再現可能にした。局面はネイティブV3 extractorが、
ターン終了処理とポケモンチェック後、次ターン開始前にだけ採取する。教師はそのターンを
終了したプレイヤー視点の最終勝敗であり、各対局の全サンプル重みの合計を1とした。

分割は対局単位で、最初の80%をtrain、次の10%をvalidation、最後の10%をtestとした。
同じ対局の局面が複数splitへ入ることはない。20 epochのうち最後の6 epochを量子化学習とし、
validationが最良の量子化可能チェックポイントを採用した。

## 未見test結果

| 指標 | 結果 |
| --- | ---: |
| 量子化モデルMSE | 0.733378 |
| 常時0モデルMSE | 1.000000 |
| 勝敗符号正解率 | 71.79% |
| C++とPython整数reference | bit一致 |
| 全採用ゲート | 合格 |

モデルは固定デッキ自己対戦方策の最終勝敗を学習している。完全探索が証明するのは、この固定された
量子化評価値に対する厳密な期待値であり、評価器そのものが真の最善プレイ勝率であることではない。
より強い方策のリプレイを得た場合は、同じV3 extractorでデータを再生成して置き換える。

## 再生成

```powershell
python tools/generate_fixed_deck_turn_end_dataset.py data/v3-fixed-selfplay.jsonl --games 2000
python tools/train_turn_end_evaluator.py data/v3-fixed-selfplay.jsonl data/evaluator.bin `
  --manifest data/v3-fixed-selfplay.jsonl.manifest.json --epochs 20 --qat-epochs 6 `
  --batch-size 1024 --require-gates
```
