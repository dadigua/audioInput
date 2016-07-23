#include "StdAfx.h"
#include "AudioInputInterface.h"
#include "audioInput.h"

AudioInputInterface::AudioInputInterface(void)
{
	ai = new AudioInput;
	ai->setCallback(defaultCallback, this);
}


AudioInputInterface::~AudioInputInterface(void)
{
	delete ai;
}

size_t AudioInputInterface::enumAudioRecorders(vector<AudioRecorderDevice>& recorders)
{
	return AudioInput::enumAudioRecorders(recorders);
}

bool AudioInputInterface::openDevice(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel)
{
	return ai->openDevice(dev, channels, sampleRate, bitsPerSampleChannel);
}

bool AudioInputInterface::startRecord()
{
	return ai->startRecord();
}

void AudioInputInterface::stopRecord()
{
	ai->stopRecord();
}

void AudioInputInterface::setCallback(AI_RECORD_CALLBACK cbfunc, void* userdata)
{
	ai->setCallback(cbfunc, userdata);
}

void AudioInputInterface::defaultCallback(const void* pcm, size_t bytes, void* userdata)
{
	AudioInputInterface* aii = (AudioInputInterface*)userdata;
	aii->onRecord(pcm, bytes);
}

void AudioInputInterface::onRecord(const void* pcm, size_t bytes)
{

}