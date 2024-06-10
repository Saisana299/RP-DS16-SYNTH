# RP-DS16-SYNTH
[![GitHub license](https://img.shields.io/badge/RP--DS16-Rev.2.0-seagreen)](https://github.com/Saisana299/RP-DS16)　

RP2040を利用したWavetableシンセ「RP-DS16」のシンセ部  
音の生成、DACの制御

## 概要
- RP2040
    - オーバークロック - 280MHz
    - CTRLとの通信に I2C0 を使用
- DAC
    - PCM5102A - [LCSC](https://www.lcsc.com/product-detail/ADC-DAC-Specialized_Texas-Instruments-PCM5102APWR_C107671.html)
    - 48000Hz/16bit
    - RP2040側 I2S を使用
- 機能
    - 音の生成
    - DAC制御

## GPIO
| RP2040 | CTRL | Note |
|:---:|:---:|:---------:|
| GP0 | SDA | - |
| GP1 | SCL | - |

| RP2040 | UART | Note |
|:---:|:---:|:---------:|
| GP8 | RX | - |
| GP9 | TX | - |

| RP2040 | DAC | Note |
|:---:|:---:|:---------:|
| GP20 | DIN | SDATA |
| GP21 | BCK | BCLK |
| GP22 | LRCK | LRCLK |