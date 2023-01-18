#pragma once
// ÓÃÓÚ¼ÆËãÒ»¸ö¶ÁÐ´»º´æÀïµÄÓÐÐ§Êý¾Ý´óÐ¡
long func_cacl_read_write_buffer_data_size(long lBufferSize, long lReadPos, long lWritePos);

// ½øÐÐÉùÒô´¦Àí£¬½ÏÎªºÄÊ±£¬ÔÚµ¥¶ÀµÄÏß³ÌÀï½øÐÐ£¬±ÜÃâÖ÷Ïß³Ì¿¨¶Ù±¬Òô
void func_do_voice_transfer_worker(
	int iNumberOfChanel,					// Í¨µÀÊýÁ¿
	double dProjectSampleRate,				// ÏîÄ¿²ÉÑùÂÊ

	long lModelInputOutputBufferSize,		// Ä£ÐÍÊäÈëÊä³ö»º³åÇø´óÐ¡
	float* fModeulInputSampleBuffer,		// Ä£ÐÍÊäÈë»º³åÇø
	long* lModelInputSampleBufferReadPos,	// Ä£ÐÍÊäÈë»º³åÇø¶ÁÖ¸Õë
	long* lModelInputSampleBufferWritePos,	// Ä£ÐÍÊäÈë»º³åÇøÐ´Ö¸Õë

	float* fModeulOutputSampleBuffer,		// Ä£ÐÍÊä³ö»º³åÇø
	long* lModelOutputSampleBufferReadPos,	// Ä£ÐÍÊä³ö»º³åÇø¶ÁÖ¸Õë
	long* lModelOutputSampleBufferWritePos,	// Ä£ÐÍÊä³ö»º³åÇøÐ´Ö¸Õë

	float* fPitchChange,					// Òôµ÷±ä»¯ÊýÖµ
	bool* bCalcPitchError,					// ÆôÓÃÒôµ÷Îó²î¼ì²â

	std::vector<roleStruct> roleStructList,	// ÅäÖÃµÄ¿ÉÓÃÒôÉ«ÁÐ±í
	int* iSelectRoleIndex,					// Ñ¡ÔñµÄ½ÇÉ«ID
	FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLLÄÚ²¿SrcSimple·½·¨

	bool* bEnableSOVITSPreResample,			// ÆôÓÃSOVITSÄ£ÐÍÈë²ÎÒôÆµÖØ²ÉÑùÔ¤´¦Àí
	int iSOVITSModelInputSamplerate,		// SOVITSÄ£ÐÍÈë²Î²ÉÑùÂÊ
	bool* bEnableHUBERTPreResample,			// ÆôÓÃHUBERTÄ£ÐÍÈë²ÎÒôÆµÖØ²ÉÑùÔ¤´¦Àí
	int iHUBERTInputSampleRate,				// HUBERTÄ£ÐÍÈë²Î²ÉÑùÂÊ

	bool* bRealTimeModel,					// Õ¼Î»·û£¬ÊµÊ±Ä£Ê½
	bool* bDoItSignal,						// Õ¼Î»·û£¬±íÊ¾¸ÃworkerÓÐ´ý´¦ÀíµÄÊý¾Ý
	bool* bEnableDebug,
	juce::Value vServerUseTime,

	bool* bWorkerNeedExit,					// Õ¼Î»·û£¬±íÊ¾workerÏß³ÌÐèÒªÍË³ö
	std::mutex* mWorkerSafeExit				// »¥³âËø£¬±íÊ¾workerÏß³ÌÒÑ¾­°²È«ÍË³ö
);
