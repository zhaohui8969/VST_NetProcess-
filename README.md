# 基于异步网络的VST声音处理插件

借助该VST3插件，可以实现将DAW的音频信号通过HTTP协议发送到音频处理程序中，用以实现 Server/Client 结构的音频信号处理系统

# 免责声明

该VST插件是一种音频处理工具，依赖DAW软件运行，旨在为用户提供娱乐和创意用途，请注意，我不鼓励任何非法、欺诈、违法或不道德的活动。

在使用该插件之前，请务必了解并遵守本免责声明中的以下条款：

- 该软件仅供娱乐和创意用途，任何非法用途均不被允许。用户应该遵守所有相关法律法规，以确保软件的合法使用。

- 该软件所产生的所有声音、音频和内容均为用户自行负责。用户应该保证任何使用该软件所产生的内容均符合当地的法律和道德规范。

- 我不对任何因使用该软件而产生的任何法律或经济责任负责。

- 我不保证该软件的功能一定符合用户的需求，也不保证该软件一定能够无误运行。如果用户遇到任何问题或错误，我将尽力为用户提供技术支持。

- 我保留随时更改、修改或终止该软件的权利，而无需通知用户。任何这样的更改或修改都将在我的网站或社交媒体上公布。

请注意，使用改插件将被视为您已经同意上述所有免责声明和条款。如果您不同意这些条款，请勿使用本软件。

# 更新

## 2023-02-22 v3.0

在实时模式进行了一些尝试，目前实时模式主要有以下问题以及解决方案：

1.过短的音频切片使得模型输出声音很奇怪，此处通过在每个前片前额外加入上一句音频，使得算法模型输入长度增加，模型能或得到足够长的信息，不会生成过于奇怪的声音，并在VST端对两个音频做首尾交叉淡化降低音频切片衔接的突兀感，但是目前仍然有些许爆音，在纯Python端做交叉淡化效果挺好，但VST里有瑕疵，估计我写的代码有问题，欢迎大佬来优化

建议的参数设置

	最长音频切片时长：0.25s
	交叉淡化时长: 0.25s

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
| 实时模式  | 关 | 实时模式（可用，有略微咔哒声，有明显延迟），关闭后为逐句模式 |
| 调试日志  | 关 | Debug开关（保持关闭就行） |
| 最长音频切片时长  | 0.8s | 最长音频切片的长度，单位是秒 |
| 变调  | 14 | 变调（这个参数直接传给后端） |
| 最长静音时长  | 0.17s | 逐句模式下有效，单位是秒，当0.17秒内没有声音的时候，认为句子结束，开始调用模型 |
| 交叉淡化时长  | 0 | （实验性参数，保持为0就好） |
| 切换角色  | role | 角色切换，切换是实时生效的，角色配置在netProcessConfig.json的roleList字段中 |
| 算法耗时  | 0ms | 每次调用算法的耗时，单位毫秒 |
| 丢弃数据时长  | 0ms | （实时模式下因为网络波动而丢弃的数据长度） |

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

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=zhaohui8969/VST_NetProcess-&type=Date)](https://star-history.com/#zhaohui8969/VST_NetProcess-&Date)

## 联系方式

QQ:896919430

Email:natas_hw@163.com

Bilibili:[串串香火锅](https://space.bilibili.com/4958385)
