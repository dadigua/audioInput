//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

//Example Usage
/*
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
*/


#ifndef AUDIO_INPUT_INTERFACE_H
#define AUDIO_INPUT_INTERFACE_H

#include <Windows.h>
#include <string>
#include <vector>
using namespace std;

typedef enum AudioRecorderType
{
	AR_DirectSound,
	AR_DirectShow,
	AR_Loopback
}AudioRecorderType;

class AudioRecorderDevice
{
public:
	AudioRecorderDevice();
	~AudioRecorderDevice();

public:
	wstring				devName;
	GUID				devID;
	AudioRecorderType	devType;
};

typedef void (__cdecl* AI_RECORD_CALLBACK)(const void* pcm, size_t bytes, void* userdata);

class AudioInput;

class AudioInputInterface
{
public:
	AudioInputInterface(void);
	virtual ~AudioInputInterface(void);
	static size_t enumAudioRecorders(vector<AudioRecorderDevice>& recorders);
	
	/* Open an audio recorder
	*  @param dev. The device which is get from enumAudioRecorders
	*  @param channels,sampleRate,bitsPerSampleChannel. For directsound device
	*   these parameters is IN. For example, (1,44100,16) (2,48000,16) etc.
	*   Otherwise(Directshow, Loopback) these parameters is OUT.
	*/
	bool openDevice(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel);
	bool startRecord();
	void stopRecord();
	
	/* You can get pcm data from onRecord or callback function which is set by setCallback
	*  Typically, for Directsound and Directshow device, the pcm is SHORT,
	*  for Loopback device, the pcm is FLOAT.
	*/
	virtual void onRecord(const void* pcm, size_t bytes);
	void setCallback(AI_RECORD_CALLBACK cbfunc, void* userdata);
protected:
	AudioInput* ai;
	static void defaultCallback(const void* pcm, size_t bytes, void* userdata);
};

#endif