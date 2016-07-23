// example.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <assert.h>
#include "../audioInput/AudioInputInterface.h"
#ifdef _DEBUG
#pragma comment(lib, "../Debug/audioInput.lib")
#else
#pragma comment(lib, "../Release/audioInput.lib")
#endif

class AudioRecorder:public AudioInputInterface
{
public:
	AudioRecorder(){}
	~AudioRecorder(){}
	void onRecord(const void* pcm, size_t bytes)
	{
		printf("override onRecord %d\n", bytes);
	}
};

static void recordcallback(const void* pcm, size_t bytes, void*)
{
	printf("callback %d\n", bytes);
}

int _tmain(int argc, _TCHAR* argv[])
{
	vector<AudioRecorderDevice> devs;
	AudioRecorder::enumAudioRecorders(devs);
	assert(devs.size() > 0);
	AudioRecorder ar;
	//ar.setCallback(recordcallback, NULL);
	int channels = 2, sampleRate = 44100, bits = 16;
	ar.openDevice(devs[0], channels, sampleRate, bits);
	printf("channels: %d, sampleRate: %d, bits: %d\n", channels, sampleRate, bits);
	ar.startRecord();
	while(1)
	{
		if('q' == getchar())
		{
			break;
		}
	}
	return 0;
}

