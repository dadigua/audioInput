# audioInput
an audio record library for windows

Example Usage
//get devices
vector<AudioRecorderDevice> devs;
AudioInputInterface::enumAudioRecorders(devs);

//create an AudioInputInterface object
AudioInputInterface ai;

//set callback function
ai.setCallback(callbackfunc, userdata);

//open device
int channels = 2, sampleRate = 48000, bitsPerSampleChannel = 16;
ai.openDevice(devs[0], channels, sampleRate, bitsPerSampleChannel);

//now start record, you can get pcm data from callbackfunc
ai.startRecord();

//stop record
ai.stopRecord();


//If you don't like callback, please inherit AudioInputInterface, and override onRecord function

class AudioRecorder : public AudioInputInterface
{
public:
	AudioRecorder(){}
	~AudioRecorder(){}
	void onRecord(const void* pcm, size_t bytes)
	{
		//do what you want
	}
};

//Typically, for Directsound and Directshow devices, the pcm format is SHORT,
//for Loopback devices, the pcm format is FLOAT.