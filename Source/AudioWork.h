#pragma once
// 用于计算一个读写缓存里的有效数据大小
long func_cacl_read_write_buffer_data_size(long lBufferSize, long lReadPos, long lWritePos);

// 进行声音处理，较为耗时，在单独的线程里进行，避免主线程卡顿爆音
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// 通道数量
	double dProjectSampleRate,				// 项目采样率

	long lModelInputOutputBufferSize,		// 模型输入输出缓冲区大小
	float* fModeulInputSampleBuffer,		// 模型输入缓冲区
	long* lModelInputSampleBufferReadPos,	// 模型输入缓冲区读指针
	long* lModelInputSampleBufferWritePos,	// 模型输入缓冲区写指针

	float* fModeulOutputSampleBuffer,		// 模型输出缓冲区
	long* lModelOutputSampleBufferReadPos,	// 模型输出缓冲区读指针
	long* lModelOutputSampleBufferWritePos,	// 模型输出缓冲区写指针

	//float* fPrefixLength,					// 前导缓冲区时长(s)
	//float* fDropSuffixLength,				// 丢弃的尾部时长(s)
	float* fPitchChange,					// 音调变化数值
	bool* bCalcPitchError,					// 启用音调误差检测

	std::vector<roleStruct> roleStructList,	// 配置的可用音色列表
	int* iSelectRoleIndex,					// 选择的角色ID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL内部SrcSimple方法

	bool* bEnableSOVITSPreResample,			// 启用SOVITS模型入参音频重采样预处理
	int iSOVITSModelInputSamplerate,		// SOVITS模型入参采样率
	bool* bEnableHUBERTPreResample,			// 启用HUBERT模型入参音频重采样预处理
	int iHUBERTInputSampleRate,				// HUBERT模型入参采样率

	bool* bRealTimeModel,					// 占位符，实时模式
	bool* bDoItSignal,						// 占位符，表示该worker有待处理的数据
	bool* bEnableDebug,						// 占位符，启用DEBUG输出
	juce::Value vServerUseTime,				// UI变量，显示服务调用耗时
	juce::Value vDropDataLength,			// UI变量，显示实时模式下丢弃的音频数据长度

	bool* bWorkerNeedExit,					// 占位符，表示worker线程需要退出
	std::mutex* mWorkerSafeExit				// 互斥锁，表示worker线程已经安全退出
);
