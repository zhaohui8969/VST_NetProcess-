# 基于异步网络IO的VST声音处理插件

借助该VST3插件，可以实现将DAW的音频信号通过HTTP协议发送到音频处理程序中，用以实现C/S结构的音频信号处理系统

举例：

结合[AI猫雷](https://github.com/IceKyrin/sovits_f0_infer/tree/main)项目，可以将猫雷变声器集成到你的DAW中

# 更新

## 2023-02-02 v2.0

使用JUCE框架重新开发，提高了在不同DAW里的兼容性，例如AU、StudioOne、Reaper

！！！注意：更换了配置文件的路径！！！，新路径为`C:\Program Files\Common Files\VST3\NetProcessJUCEVersion\netProcessConfig.json`

# 安装

## 文件复制

在release页面下载最新的压缩包，解压后会有3个文件`netProcessConfig.json`，`NetProcessJUCEVersion.vst3`，`samplerate.dll`，将这些文件放在如下目录下`C:\Program Files\Common Files\VST3\NetProcessJUCEVersion`，！！！！注意路径一定要对，不然可能DAW扫描不到VST插件

## 配置文件修改，多角色模型配置

按需要修改`netProcessConfig.json`中的内容，其中每个字段的定义如下

|  字段   | 默认值  | 说明 |
|  ----  | ----  | --- |
| configVersion  | "2.0" | 配置文件版本号（勿改） |
| fSampleVolumeWorkActiveVal  | 0.05 | 静音检测音量阈值（范围0-1），低于这个阈值被认为是静音 |
| bEnableSOVITSPreResample  | false | 启用对SOVITS输入音频提前重采样，把重采样流程放到VST插件中，如果DAW里处理速度比后端快，可以开启 |
| iSOVITSModelInputSamplerate  | 22050 | SOVITS输入音频提前重采样频率 |
| bEnableHUBERTPreResample  | false | 启用对HUBERT输入音频提前重采样，，把重采样流程放到VST插件中，如果DAW里处理速度比后端快，可以开启，如果后端不支持的话，这里用false |
| iHUBERTInputSampleRate  | 16000 | HUBERT输入音频提前重采样频率 |
| roleList  |  | 多角色配置列表，至少放一个，不然插件报错打不开 |

多角色配置字段说明，每个说话人一个配置字典

|  字段   | 默认值  | 说明 |
|  ----  | ----  | --- |
| apiUrl  | "http://127.0.0.1:6844" | 后端服务地址，也可以启用多个不同的模型，然后在这里配置不同的接口地址 |
| speakId  | "0" | 说话人ID |
| name  | "XXXX" | 说话人名称（只支持字母或者数字），会显示在插件UI界面便于实时切换说话人 |

## VST以及DAW配置

插件安装完毕后会在StudioOne里按如下显示

![studioOnePlugins](./docs/plugins.png)

插件的界面如下

![main](./docs/main.png)

|  选项   | 默认值  | 说明 |
|  ----  | ----  | --- |
| Real time mode  | 关 | 实时模式（效果较差，别有太高期待），关闭后为逐句模式 |
| Debug mode  | 关 | Debug开关（保持关闭就行） |
| Max audio slice length  | 0.8s | 最长音频切片的长度，单位是秒 |
| Pitch change  | 14 | 变调（这个参数直接传给后端） |
| Max low volume length  | 0.17s | 逐句模式下有效，单位是秒，当0.17秒内没有声音的时候，认为句子结束，开始调用模型 |
| Prefix audio length  | 0 | （实验性参数，保持为0就好） |
| Drop suffix audio length  | 0 | （实验性参数，保持为0就好） |
| Change role  | role | 角色切换，切换是实时生效的，角色配置在netProcessConfig.json的roleList字段中 |
| Server use time  | 0ms | 每次调用算法的耗时，单位毫秒 |
| Drop data  | 0ms | （实时模式下因为网络波动而丢弃的数据长度） |

## DAW采样率

studioOne里工程的采样率应设置为44.1Khz

![44.1kHz](./docs/studioOneSampleRateSetting.png)

# 兼容性测试

## Studio One

Studio One版本：Windows 5.5.1.85792
插件版本：v2.0

![Studio One兼容性测试](./docs/studio_one_test.png)

## AU

AU版本：Windows 22.1.1.23
插件版本：v2.0

![AU兼容性测试](./docs/au_test.png)

## Reaper

Reaper版本：Windows v6.45
插件版本：v2.0

![Reaper兼容性测试](./docs/reaper_test.png)

## 联系方式

QQ:896919430

Bilibili:[串串香火锅](https://space.bilibili.com/4958385)


