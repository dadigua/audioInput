//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

#pragma once

#include "AudioInputInterface.h"
#include <MMSystem.h>
#include <dsound.h>
#include <objbase.h>
#include <dshow.h>
#include <uuids.h>
#include <oleauto.h>
#include <Mmdeviceapi.h>
#include <Audioclient.h>

struct ISampleGrabber;
class AudioSampleGrabberCallback;


class AudioInput
{
public:
	AudioInput(void);
	virtual ~AudioInput(void);

	static size_t enumAudioRecorders(vector<AudioRecorderDevice>& recorders);

	bool openDevice(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel);
	bool startRecord();
	void stopRecord();
	virtual void onRecord(const void* pcm, size_t bytes);
	void onRecordImpl(const void* pcm, size_t bytes);
	void setCallback(AI_RECORD_CALLBACK cbfunc, void* userdata);
protected:
	AudioRecorderDevice						recordDev;
	HANDLE									recordThread;
	bool									flagStop;
	AI_RECORD_CALLBACK						recCallback;
	void*									recUserdata;

	//DSound
	LPDIRECTSOUNDCAPTURE8					dscap8;
	LPDIRECTSOUNDCAPTUREBUFFER8				dsbuf8;
	HANDLE									dsEvents[3];

	//DShow
	ICaptureGraphBuilder2*					pCaptureGraphBuilder;
	IGraphBuilder*							pGraph;
	IMediaControl*							pControl;
	IBaseFilter*							pAudioInputFilter;
	IBaseFilter*							pGrabberF;
	IBaseFilter*							pDestFilter;
	ISampleGrabber*							pGrabber;
	AudioSampleGrabberCallback*				sgCallback;

	//Loopback
	WAVEFORMATEX*							pwfx;
	IMMDeviceEnumerator*					pEnumerator;
	IMMDevice*								pDevice;
	IAudioClient*							pAudioClient;
	IAudioCaptureClient*					pCaptureClient;
	UINT32									bufferFrameCount;
	int										bufferDuration;


	int										nChannels;
	int										nSampleRate;
	int										nBitsPerSampleChannel;
	int										nRecordBufferSize;
	ULONG64									nRecordBytes;

	static UINT comInitCount;
	static bool comInit();
	static bool comUnInit();
	static DWORD WINAPI dsRecordProc(LPVOID lParam);
	static DWORD WINAPI lbRecordProc(LPVOID lParam);
	HRESULT getDevice(IBaseFilter** gottaFilter, const wstring& devName);
	void destroyGraph();
	void stopDShowDevice();
	void nukeDownstream(IBaseFilter *pBF);
	bool openDeviceDirectSoundImpl(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel);
	bool openDeviceDirectShowImpl(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel);
	bool openDeviceLoopbackImpl(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel);
};

