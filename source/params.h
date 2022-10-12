#pragma once

enum
{
	kEnableTwiceRepeat = 100,
	kTwiceRepeatIntvalTime = 101,
	kMaxSliceLength = 102,
	kPitchChange = 103,
	//kPrefixBufferLength = 104,
	kEnabelPitchErrorCalc = 105,
	kSelectRole = 106
};

#define defaultEnableTwiceRepeat false
#define defaultEnabelPitchErrorCalc false
#define defaultTwiceRepeatIntvalTime 0.f
#define maxTwiceRepeatIntvalTime 2.0f
#define defaultMaxSliceLength 0.2f
#define maxMaxSliceLength 10.f
#define defaultPitchChange 0.f
#define maxPitchChange 30.f
#define minPitchChange -30.f
#define defaultSelectRole 0
//#define defaultPrefixBufferLength 0.01f
//#define maxPrefixBufferLength 5.f
//#define minPrefixBufferLength 1.f
