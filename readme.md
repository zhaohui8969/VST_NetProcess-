# 基于异步网络IO的VST声音处理插件

借助该VST3插件，可以实现将DAW的音频信号通过HTTP协议发送到音频处理程序中，用以实现C/S结构的音频信号处理系统

举例：

结合[AI猫雷](https://github.com/IceKyrin/sovits_f0_infer/tree/main)项目，可以将猫雷变声器集成到你的DAW中

## VST以及DAW配置

插件安装完毕后会在StudioOne里按如下显示

![studioOnePlugins](./doc/plugins.png)

插件的界面如下

![UI](./doc/UI.png)

`是否二次重复监听`启用后，会将耳返的声音重复两遍。

`二次监听间隔`用于在二次重复监听启用后在两次耳返中插入一个静音间隔。

`最长音频切片`用于控制在调用HTTP接口前，最长的音频切片大小，过短的切片可能会导致爆音，过长的值则增加耳返延迟。

`变调`是猫雷AI模型的变调入参，旋钮范围是0-30。

studioOne里工程的采样率应设置为44.1Khz

![44.1kHz](./doc/studioOneSampleRateSetting.png)

需建立`C:/temp/vst/`目录用于存储配置文件

多角色模型的配置文件模板在[doc/netProcessConfig.json](doc/netProcessConfig.json)，需要复制到`C:/temp/vst/`目录下

## 联系方式

QQ:896919430

Bilibili:[串串香火锅](https://space.bilibili.com/4958385)
