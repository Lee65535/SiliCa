# SiliCa

`SiliCa` は、JIS X 6319-4（`FeliCa`）互換の非接触型 IC カードです。  
マイコンとして `ATtiny1616` を採用しており、外部電源を必要とせず、カードリーダからの給電によって動作します。

![3D View](./img/1_1/3d_view.png)

## About This Fork

This fork of SiliCa modifies the firmware so that the device can be used to emulate Amusement IC cards. It will work on bemani games and most bootleg / private servers for SEGA games, but due to technical limitations it is impossible to be used on legit SEGA arcades connected to official servers.

To emulate an Amusement IC, write the IDm as well as data in SPAD_0 to SiliCa, set the system code to 0x88B4, and set service codes to 0x0000 and 0x000B.

## 概要

`SiliCa` は、最小限のハードウェア構成で、 `FeliCa` プロトコルをファームウェアによって再現し、`FeliCa Standard` や `FeliCa Lite-S` のエミュレーションを実現することを目的としています。

また、`SiliCa` では、実際の `FeliCa` カードで書き換えが不可能な `IDm` やシステムコードなどのパラメータを自由に書き換えることができます。

詳細な仕様や使用方法については、寄稿した同人誌 `rand_r(&v3)` をご参照ください。

[技術書典マーケット](https://techbookfest.org/product/an6q3RzcSM0RcWQBevrEM9)
[BOOTH 紙版](https://unset-histfile.booth.pm/items/7650376)
[BOOTH 電子版](https://unset-histfile.booth.pm/items/7650390)

## 回路図

![Schematic](./img/1_1/schematic.png)

## 注意事項

本プロジェクトは、学術的および実験的な目的で提供されており、商業利用や実際の運用を目的としたものではありません。

---

```
FeliCaは、ソニー株式会社が開発した非接触ICカードの技術方式です。
FeliCaは、ソニーグループ株式会社の登録商標です。
本文では™、®は明記していません。
```
