#pragma once

long long func_get_timestamp();

std::vector<float> hanning(int n);

// 进行声音处理，较为耗时，在单独的线程里进行，避免主线程卡顿爆音
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// 通道数量
	double dProjectSampleRate,				// 项目采样率

	std::vector<JOB_STRUCT>* modelInputJobList, // 模型输入队列
	juce::CriticalSection* modelInputJobListLock,		// 模型输入队列锁
	
	std::vector<JOB_STRUCT>* modelOutputJobList, // 模型输出队列
	juce::CriticalSection* modelOutputJobListLock,		// 模型输出队列锁

	long lCrossFadeLength,
	std::vector<float>* hanningWindow,

	float* fPitchChange,					// 音调变化数值

	std::vector<roleStruct> roleStructList,	// 配置的可用音色列表
	int* iSelectRoleIndex,					// 选择的角色ID

	bool* bEnableDebug,						// 占位符，启用DEBUG输出
	juce::Value vServerUseTime,				// UI变量，显示服务调用耗时
	float *fServerUseTime,

	bool* bWorkerNeedExit,					// 占位符，表示worker线程需要退出
	std::mutex* mWorkerSafeExit				// 互斥锁，表示worker线程已经安全退出
);
