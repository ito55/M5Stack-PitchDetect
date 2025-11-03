# M5Stack Audio Pitch Detector

このプロジェクトは、M5Stack Core2 の内蔵マイクから音声を取得し、単音（モノフォニック）の音高を推定して、対応する MIDI Note On / Note Off メッセージを送信します。想定する MIDI ノート範囲は 58〜75 です。それ以外の音は想定していません。

機能
- 単音の音高推定（和音は対象外）
- Goertzel フィルタバンクによる音高推定（固定ノート集合に対して高速かつ適切）
- M5 Unit MIDI 経由の MIDI 出力、または UART（31250 baud）によるフォールバック
- 内蔵ディスプレイに音の有無、検出された MIDI ノート、信号パワーを表示
- Note On/Off の挙動:
  - 音が検出され安定したときに Note On を送信
  - 音が止まったと判断したときに Note Off を送信
  - 音が継続している間に音高が変化した場合は、前のノートを Note Off、次のノートで Note On

リポジトリ構成
- src\M5Stack_PitchDetect_ArdSketch\M5Stack_PitchDetect_ArdSketch.ino — メインの Arduino スケッチ（サンプル）
- README.md — 英語の README（本ファイルと対）
- README.ja.md — 日本語 README（このファイル）

必要なもの
- M5Stack Core2（ESP32 ベース）
- M5 Unit MIDI（推奨）または外部 MIDI 接続（UART2 経由）
- Arduino IDE
- ライブラリ:
  - M5Unified（M5GFX を含む） — https://github.com/m5stack/M5Unified
  - ESP32 用 Arduino コア

ハードウェアと ADC に関する注意
- スケッチは I2S の ADC 内蔵モードを使用します。Core2 のリビジョンによって内蔵マイクや ADC のチャネル割当が異なる場合があります。i2s_set_adc_mode(...) の ADC チャネルや I2S の設定を適宜調整してください。
- M5Unified の Unit MIDI が Stream ライクのインターフェースを提供する前提になっています。バージョン差異がある場合、スケッチは UART2（Serial2、31250 bps）にフォールバックします。配線や使用するピンは環境に合わせて調整してください。

クイックスタート（Arduino IDE）
1. ESP32 Arduino core をインストールし、ボードを M5Stack Core2 に設定します。
2. M5Unified ライブラリをインストールします。
3. `M5Stack_PitchDetect_ArdSketch.ino` を開きます。
4. 適切なボードとポートを選択します。
5. ビルドし、M5Core2 にアップロードします。
6. M5 Unit MIDI を接続するか、TX（GPIO17）を MIDI 入力へ（適切な MIDI DIN インターフェースとアイソレーションを介して）接続します。
7. MIDI ノート 58〜75 の範囲の単音を鳴らし、画面と MIDI 出力を確認します。

設定とチューニング
- SAMPLE_RATE や BLOCK_SIZE：レイテンシと周波数分解能のトレードオフです（デフォルト 8 kHz、256 サンプル）。
- POWER_THRESHOLD：マイク感度や環境ノイズに合わせて調整してください。高すぎると検出されず、低すぎると誤検出が増えます。
- NOTE_DETECTION_RATIO：倍音による誤検出を低減するために、1 位のマグニチュードが 2 位より十分に大きいことを要求します。
- STABLE_FRAMES_TO_CONFIRM / SILENCE_FRAMES_FOR_OFF：デバウンスと Note Off の遅延を調整します。

トラブルシューティング
- 音が検出されない / 常にオフ:
  - Core2 の ADC チャネルや I2S/ADC のマッピングを確認してください。
  - POWER_THRESHOLD を下げて感度を上げてみてください。
- 音高が不安定または誤検出:
  - BLOCK_SIZE を大きくして周波数分解能を改善する、あるいは STABLE_FRAMES_TO_CONFIRM を増やす。
  - マイクやハードウェアの問題を確認する。
- MIDI が出力されない:
  - Unit MIDI を使用している場合、M5.Units.getUnit("midi") でユニットが検出されるか確認してください。
  - UART フォールバックを使用している場合は、Serial2 の配線および MIDI DIN インターフェースが正しいか確認してください。
