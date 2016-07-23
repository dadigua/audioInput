//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

#include "StdAfx.h"
#include "AudioInput.h"


#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")


#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define SAFE_RELEASE(punk)  \
	if ((punk) != NULL)  \
				{ (punk)->Release(); (punk) = NULL; }

MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SampleCB( 
		double SampleTime,
		IMediaSample *pSample) = 0;

	virtual HRESULT STDMETHODCALLTYPE BufferCB( 
		double SampleTime,
		BYTE *pBuffer,
		long BufferLen) = 0;

};

MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SetOneShot( 
		BOOL OneShot) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetMediaType( 
		const AM_MEDIA_TYPE *pType) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType( 
		AM_MEDIA_TYPE *pType) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetBufferSamples( 
		BOOL BufferThem) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer( 
		/* [out][in] */ long *pBufferSize,
		/* [out] */ long *pBuffer) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCurrentSample( 
		/* [retval][out] */ IMediaSample **ppSample) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetCallback( 
		ISampleGrabberCB *pCallback,
		long WhichMethodToCallback) = 0;

};
EXTERN_C const CLSID CLSID_SampleGrabber;
EXTERN_C const IID IID_ISampleGrabber;
EXTERN_C const CLSID CLSID_NullRenderer;

void MyFreeMediaType(AM_MEDIA_TYPE& mt)
{
	if (mt.cbFormat != 0)
	{
		CoTaskMemFree((PVOID)mt.pbFormat);
		mt.cbFormat = 0;
		mt.pbFormat = NULL;
	}
	if (mt.pUnk != NULL)
	{
		mt.pUnk->Release();
		mt.pUnk = NULL;
	}
}

void MyDeleteMediaType(AM_MEDIA_TYPE *pmt)
{
	if (pmt != NULL)
	{
		MyFreeMediaType(*pmt);
		CoTaskMemFree(pmt);
	}
}

class AudioSampleGrabberCallback : public ISampleGrabberCB
{
public:
	AudioSampleGrabberCallback(AudioInput* ai)
	{
		this->ai = ai;
	}
	~AudioSampleGrabberCallback()
	{
	}
	STDMETHODIMP_(ULONG) AddRef() { return 1; }
	STDMETHODIMP_(ULONG) Release() { return 2; }
	STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject)
	{
		*ppvObject = static_cast<ISampleGrabberCB*>(this);
		return S_OK;
	}

	STDMETHODIMP SampleCB(double Time, IMediaSample *pSample)
	{
		BYTE* ptrBuffer = 0;
		HRESULT hr = pSample->GetPointer(&ptrBuffer);  
		if(hr == S_OK)
		{
			long pcmBytes = pSample->GetActualDataLength();
			ai->onRecordImpl(ptrBuffer, pcmBytes);
		}
		return S_OK;
	}

	STDMETHODIMP BufferCB(double Time, BYTE *pBuffer, long BufferLen)
	{
		return E_NOTIMPL;
	}

	AudioInput* ai;
};

AudioRecorderDevice::AudioRecorderDevice()
{
}

AudioRecorderDevice::~AudioRecorderDevice()
{
}

UINT AudioInput::comInitCount = 0;
AudioInput::AudioInput(void)
{
	dsbuf8 = NULL;
	dscap8 = NULL;
	dsEvents[0] = dsEvents[1] = dsEvents[2] = NULL;
	recordThread = NULL;
	pwfx = NULL;
	pEnumerator = NULL;
	pDevice = NULL;
	pAudioClient = NULL;
	pCaptureClient = NULL;
	pCaptureGraphBuilder = NULL;
	pGraph = NULL;
	pControl = NULL;
	pAudioInputFilter = NULL;
	pGrabberF = NULL;
	sgCallback = NULL;
	pGrabber = NULL;
	pDestFilter = NULL;
	flagStop = true;
	recCallback = NULL;
	recUserdata = NULL;
	comInit();
}


AudioInput::~AudioInput(void)
{
	stopRecord();
	comUnInit();
}

bool AudioInput::comInit()
{
	if(!comInitCount)
	{
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
	}
	comInitCount++; 
	return true;
}

bool AudioInput::comUnInit()
{
	if(comInitCount > 0)comInitCount--;
	if(!comInitCount)
	{
		CoUninitialize();
		return true;	
	}
	return false;
}

static BOOL __stdcall DirectSoundEnumCallback(LPGUID pGuid, LPCTSTR lpszDescrip, LPCTSTR pModule, LPVOID pCtx)
{
	if(pGuid)
	{
		vector<AudioRecorderDevice>* pDevs = (vector<AudioRecorderDevice>*)pCtx;
		AudioRecorderDevice dev;
		memcpy(&dev.devID, pGuid, sizeof(GUID));
		dev.devName = lpszDescrip;
		dev.devType = AR_DirectSound;
		pDevs->push_back(dev);
	}
	return TRUE;
}

size_t AudioInput::enumAudioRecorders(vector<AudioRecorderDevice>& recorders)
{
	vector<AudioRecorderDevice> rec;
	comInit();

	//directsound devices
	DirectSoundCaptureEnumerate(DirectSoundEnumCallback, (LPVOID)&rec);

	//directshow devices
	vector<AudioRecorderDevice> dsoundDevs = rec;
	ICreateDevEnum *pDevEnum = NULL;
	IEnumMoniker *pEnum = NULL;	
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
		CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, 
		reinterpret_cast<void**>(&pDevEnum));
	if (SUCCEEDED(hr))
	{
		hr = pDevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &pEnum, 0);
		if(hr == S_OK)
		{
			IMoniker *pMoniker = NULL;
			while(pEnum->Next(1, &pMoniker, NULL) == S_OK)
			{
				IPropertyBag *pPropBag;
				hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)(&pPropBag));
				if (FAILED(hr))
				{
					pMoniker->Release();
					continue;
				} 
				VARIANT varName;
				VariantInit(&varName);
				hr = pPropBag->Read(L"Description", &varName, 0);
				if (FAILED(hr)) hr = pPropBag->Read(L"FriendlyName", &varName, 0);
				if (SUCCEEDED(hr))
				{
					hr = pPropBag->Read(L"FriendlyName", &varName, 0);
					//Usually, a directsound device is also a directshow device
					bool bAdd = true;
					for(UINT i=0;i<dsoundDevs.size();i++)
					{
						AudioRecorderDevice dev = dsoundDevs[i];
						if(dev.devName.find(varName.bstrVal) == 0)
						{
							bAdd = false;
							break;
						}
					}
					if(bAdd)
					{
						AudioRecorderDevice dev;
						dev.devName = varName.bstrVal;
						dev.devType = AR_DirectShow;
						rec.push_back(dev);
					}
				}
				pPropBag->Release();
				pPropBag = NULL;
				pMoniker->Release();
				pMoniker = NULL;
			}   
			pDevEnum->Release();
			pDevEnum = NULL;
			pEnum->Release();
			pEnum = NULL;
		}
	}

	//loopback
	OSVERSIONINFO ovi;
	ovi.dwOSVersionInfoSize = sizeof(ovi);
	if(GetVersionEx(&ovi) && ovi.dwMajorVersion > 5)
	{
		AudioRecorderDevice dev;
		dev.devName = L"Loopback";
		dev.devType = AR_Loopback;
		rec.push_back(dev);
	}
	comUnInit();
	recorders = rec;
	return rec.size();
}

bool AudioInput::openDevice(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel)
{
	if(recordThread)return false;
	switch(dev.devType)
	{
	case AR_DirectSound:
		return openDeviceDirectSoundImpl(dev, channels, sampleRate, bitsPerSampleChannel);
	case AR_DirectShow:
		return openDeviceDirectShowImpl(dev, channels, sampleRate, bitsPerSampleChannel);
	case AR_Loopback:
		return openDeviceLoopbackImpl(dev, channels, sampleRate, bitsPerSampleChannel);
	default:
		break;
	}
	return false;
}

bool AudioInput::openDeviceDirectSoundImpl(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel)
{
	HRESULT hr = DirectSoundCaptureCreate8(&dev.devID, &dscap8, NULL);
	if(FAILED(hr))return false;

	nRecordBufferSize = 4096*channels*bitsPerSampleChannel/8;
	DSCBUFFERDESC dscbd;
	WAVEFORMATEX wft = {WAVE_FORMAT_PCM, channels, sampleRate, sampleRate*channels*bitsPerSampleChannel/8, channels*bitsPerSampleChannel/8, bitsPerSampleChannel, 0};
	dscbd.dwSize = sizeof(DSCBUFFERDESC);
	dscbd.dwFlags = 0;
	dscbd.dwBufferBytes = nRecordBufferSize*2;
	dscbd.dwReserved = 0;
	dscbd.lpwfxFormat = &wft;
	dscbd.dwFXCount = 0;
	dscbd.lpDSCFXDesc = NULL;

	LPDIRECTSOUNDCAPTUREBUFFER dsbuf;
	if(FAILED(dscap8->CreateCaptureBuffer(&dscbd, &dsbuf, NULL)))goto ds_open_failed;
	hr = dsbuf->QueryInterface(IID_IDirectSoundCaptureBuffer8, (LPVOID*)&dsbuf8);
	dsbuf->Release();
	if(FAILED(hr))goto ds_open_failed;
	LPDIRECTSOUNDNOTIFY dsnotify;
	if(FAILED(dsbuf8->QueryInterface(IID_IDirectSoundNotify, (LPVOID*)&dsnotify)))goto ds_open_failed;
	dsEvents[0] = CreateEvent(0, 0, 0, 0);
	dsEvents[1] = CreateEvent(0, 0, 0, 0);
	dsEvents[2] = CreateEvent(0, 0, 0, 0);
	DSBPOSITIONNOTIFY notify[2];
	notify[0].dwOffset = 0;
	notify[0].hEventNotify = dsEvents[0];
	notify[1].dwOffset = nRecordBufferSize;
	notify[1].hEventNotify = dsEvents[1];

	hr = dsnotify->SetNotificationPositions(2, notify);
	dsnotify->Release();
	if(FAILED(hr))goto ds_open_failed;
	recordDev = dev;
	nSampleRate = sampleRate;
	nChannels = channels;
	nBitsPerSampleChannel = bitsPerSampleChannel;
	return true;
ds_open_failed:
	SAFE_RELEASE(dsbuf8);
	SAFE_RELEASE(dscap8);
	if(dsEvents[0])
	{
		CloseHandle(dsEvents[0]);dsEvents[0] = NULL;
		CloseHandle(dsEvents[1]);dsEvents[1] = NULL;
		CloseHandle(dsEvents[2]);dsEvents[2] = NULL;
	}
	return false;
}

bool AudioInput::openDeviceDirectShowImpl(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel)
{
	IAMStreamConfig* streamConf = 0;
	AM_MEDIA_TYPE* pmt = 0;
	IMediaFilter *pMediaFilter = 0;
	HRESULT hr;
	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, 0, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void**)&pCaptureGraphBuilder);
	if(FAILED(hr))goto dshow_open_failed;
	hr = CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER,IID_IGraphBuilder, (void**)&pGraph);
	if(FAILED(hr))goto dshow_open_failed;
	hr = pCaptureGraphBuilder->SetFiltergraph(pGraph);
	if(FAILED(hr))goto dshow_open_failed;
	hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pControl);
	if(FAILED(hr))goto dshow_open_failed;
	hr = getDevice(&pAudioInputFilter, dev.devName);
	if(FAILED(hr))goto dshow_open_failed;
	hr = pGraph->AddFilter(pAudioInputFilter, dev.devName.c_str());
	if(FAILED(hr))goto dshow_open_failed;
	hr = pCaptureGraphBuilder->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Audio, pAudioInputFilter, IID_IAMStreamConfig, (void **)&streamConf);
	if(FAILED(hr))goto dshow_open_failed;
	hr = streamConf->GetFormat(&pmt);
	if(FAILED(hr))goto dshow_open_failed;
	WAVEFORMATEX *pWaveFmt =  reinterpret_cast<WAVEFORMATEX*>(pmt->pbFormat);

	nSampleRate = pWaveFmt->nSamplesPerSec;
	nChannels = pWaveFmt->nChannels;
	nBitsPerSampleChannel = pWaveFmt->wBitsPerSample;
	MyDeleteMediaType(pmt);
	pmt = 0;
	streamConf->Release();
	streamConf = 0;
	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,IID_IBaseFilter, (void**)&pGrabberF);
	if(FAILED(hr))goto dshow_open_failed;
	hr = pGraph->AddFilter(pGrabberF, L"Sample Grabber");
	if(FAILED(hr))goto dshow_open_failed;
	hr = pGrabberF->QueryInterface(IID_ISampleGrabber, (void**)&pGrabber);
	if(FAILED(hr))goto dshow_open_failed;
	pGrabber->SetOneShot(FALSE);
	pGrabber->SetBufferSamples(FALSE);
	sgCallback = new AudioSampleGrabberCallback(this);
	hr = pGrabber->SetCallback(sgCallback, 0);
	if(FAILED(hr))goto dshow_open_failed;
	hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pDestFilter);
	if(FAILED(hr))goto dshow_open_failed;
	hr = pGraph->AddFilter(pDestFilter, L"NullRenderer");
	if(FAILED(hr))goto dshow_open_failed;
	hr = pCaptureGraphBuilder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Audio, pAudioInputFilter, pGrabberF, pDestFilter);	
	if(FAILED(hr))goto dshow_open_failed;

	hr = pGraph->QueryInterface(IID_IMediaFilter, (void**)&pMediaFilter);
	if(FAILED(hr))goto dshow_open_failed;
	pMediaFilter->SetSyncSource(NULL);
	pMediaFilter->Release();
	pMediaFilter = 0;

	recordDev = dev;
	channels = nChannels;
	sampleRate = nSampleRate;
	bitsPerSampleChannel = nBitsPerSampleChannel;
	return true;
dshow_open_failed:
	if(pmt)
	{
		MyDeleteMediaType(pmt);
	}
	if(pMediaFilter)
	{
		pMediaFilter->Release();
	}
	stopDShowDevice();
	return false;
}

HRESULT AudioInput::getDevice(IBaseFilter** gottaFilter,  const wstring& devName)
{
	BOOL done = FALSE;
	ICreateDevEnum *pSysDevEnum = NULL;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void **)&pSysDevEnum);
	if(FAILED(hr))return hr;
	IEnumMoniker *pEnumCat = NULL;
	hr = pSysDevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &pEnumCat, 0);
	if(hr == S_OK)
	{
		IMoniker *pMoniker = NULL;
		ULONG cFetched;
		while ((pEnumCat->Next(1, &pMoniker, &cFetched) == S_OK) && (!done))
		{
			IPropertyBag *pPropBag;
			hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pPropBag);
			if (SUCCEEDED(hr))
			{
				VARIANT varName;
				VariantInit(&varName);
				hr = pPropBag->Read(L"FriendlyName", &varName, 0);
				if(SUCCEEDED(hr))
				{		
					hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)gottaFilter);
					if(devName == varName.bstrVal){done = TRUE;}
				}
				VariantClear(&varName);	
				pPropBag->Release();
				pPropBag = NULL;
				pMoniker->Release();
				pMoniker = NULL;
			}
		}
		pEnumCat->Release();
		pEnumCat = NULL;
	}
	pSysDevEnum->Release();
	pSysDevEnum = NULL;

	if (done)
	{
		return hr;
	} 
	else 
	{
		return VFW_E_NOT_FOUND;
	}
}

void AudioInput::destroyGraph()
{
	HRESULT hr = NULL;
	int FuncRetval = 0;
	int NumFilters = 0;
	int i = 0;
	while (hr == NOERROR)
	{
		IEnumFilters * pEnum = 0;
		ULONG cFetched;
		hr = pGraph->EnumFilters(&pEnum);
		if (FAILED(hr)) { return; }
		IBaseFilter * pFilter = NULL;
		if (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO FilterInfo={0};
			hr = pFilter->QueryFilterInfo(&FilterInfo);
			FilterInfo.pGraph->Release();
			hr = pGraph->RemoveFilter(pFilter);
			if (FAILED(hr)) {return;}
			pFilter->Release();
			pFilter = NULL;
		}
		else
		{
			break;
		}
		pEnum->Release();
		pEnum = NULL;
		i++;
	}
}

bool AudioInput::openDeviceLoopbackImpl(const AudioRecorderDevice& dev, int& channels, int& sampleRate, int& bitsPerSampleChannel)
{
	PWAVEFORMATEXTENSIBLE pwfxEx = 0;
	if(FAILED(CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator))){goto lb_open_failed;}
	if(FAILED(pEnumerator->GetDefaultAudioEndpoint(
		eRender, eConsole, &pDevice))){goto lb_open_failed;}
	if(FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
		NULL, (void**)&pAudioClient))){goto lb_open_failed;}
	if(FAILED(pAudioClient->GetMixFormat(&pwfx))){goto lb_open_failed;}
	if(FAILED(pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK, 
		REFTIMES_PER_MILLISEC*80, 
		0, pwfx, 0))){goto lb_open_failed;}
	if(FAILED(pAudioClient->GetBufferSize(&bufferFrameCount))){goto lb_open_failed;}
	bufferDuration = (double)bufferFrameCount*1000.0 / pwfx->nSamplesPerSec;
	if(FAILED(pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pCaptureClient))){goto lb_open_failed;}
	if(FAILED(pAudioClient->Start())){goto lb_open_failed;}

	if(pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
		pwfxEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
	}
	nChannels = pwfx->nChannels;
	nSampleRate = pwfx->nSamplesPerSec;
	nBitsPerSampleChannel = pwfx->wBitsPerSample;

	/*if(pwfx->wBitsPerSample == 32){
		if(pwfxEx){
			if(IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pwfxEx->SubFormat)){
			}
		}else{
			if(pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT){
			}
		}
	}*/
	recordDev = dev;
	sampleRate = nSampleRate;
	channels = nChannels;
	bitsPerSampleChannel = nBitsPerSampleChannel;
	return true;
lb_open_failed:
	CoTaskMemFree(pwfx);pwfx = NULL;
	SAFE_RELEASE(pEnumerator);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pCaptureClient);
	return false;
}

bool AudioInput::startRecord()
{
	switch(recordDev.devType)
	{
	case AR_DirectSound:
		{
			if(dsbuf8 && !recordThread)
			{
				nRecordBytes = 0;
				flagStop = false;
				recordThread = CreateThread(NULL, 0, dsRecordProc, this, 0, NULL);
				dsbuf8->Start(DSCBSTART_LOOPING);
				return true;
			}
		}
		break;
	case AR_DirectShow:
		if(pControl && SUCCEEDED(pControl->Run()))
		{
			flagStop = false;
			return true;
		}
		break;
	case AR_Loopback:
		{
			if(pCaptureClient && !recordThread)
			{
				flagStop = false;
				recordThread = CreateThread(NULL, 0, lbRecordProc, this, 0, NULL);
				return true;
			}
		}
		break;
	default:
		break;
	}
	return false;
}

void AudioInput::stopRecord()
{
	flagStop = true;
	//directsound
	if(dsbuf8)
	{
		dsbuf8->Stop();
	}
	if(dsEvents[2])
	{
		SetEvent(dsEvents[2]);
	}
	if(recordThread)
	{
		WaitForSingleObject(recordThread, INFINITE);
		CloseHandle(recordThread);
		recordThread = NULL;
	}
	SAFE_RELEASE(dsbuf8);
	SAFE_RELEASE(dscap8);
	if(dsEvents[0])
	{
		CloseHandle(dsEvents[0]);dsEvents[0] = NULL;
		CloseHandle(dsEvents[1]);dsEvents[1] = NULL;
		CloseHandle(dsEvents[2]);dsEvents[2] = NULL;
	}

	//directshow
	stopDShowDevice();

	//loopback
	CoTaskMemFree(pwfx);pwfx = NULL;
	SAFE_RELEASE(pEnumerator);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pCaptureClient);
}

DWORD WINAPI AudioInput::dsRecordProc(LPVOID lParam)
{
	AudioInput* ai = (AudioInput*)lParam;
	int timeOut = ai->nRecordBufferSize*1000*2 / (ai->nSampleRate*ai->nChannels*ai->nBitsPerSampleChannel/8);
	LPVOID data1 = NULL;
	LPVOID data2 = NULL;
	DWORD size1, size2;
	while(!ai->flagStop)
	{
		DWORD dwRet = WaitForMultipleObjects(3, ai->dsEvents, 0, timeOut);
		if(dwRet == WAIT_OBJECT_0)
		{
			ai->dsbuf8->Lock(ai->nRecordBufferSize, ai->nRecordBufferSize, &data1, &size1, &data2, &size2, 0);
			ai->onRecordImpl(data1, size1);
			ai->nRecordBytes += size1;
			ai->dsbuf8->Unlock(data1, size1, data2, size2);
		}
		else if(dwRet == WAIT_OBJECT_0+1)
		{
			ai->dsbuf8->Lock(0, ai->nRecordBufferSize, &data1, &size1, &data2, &size2, 0);
			ai->onRecordImpl(data1, size1);
			ai->nRecordBytes += size1;
			ai->dsbuf8->Unlock(data1, size1, data2, size2);
		}
		else if(dwRet == WAIT_OBJECT_0+2)
		{
			break;
		}
		else if(dwRet == WAIT_TIMEOUT)
		{
			continue;
		}
		else
		{
			break;
		}
	}
	return 0;
}

DWORD WINAPI AudioInput::lbRecordProc(LPVOID lParam)
{
	AudioInput* ai = (AudioInput*)lParam;
	UINT32	numFramesAvailable;
	UINT32	packetLength = 0;
	BYTE*	pData = 0;
	DWORD	flags;
	char*	pcmBuffer = new char[ai->bufferFrameCount*2*ai->nChannels*ai->nBitsPerSampleChannel/8];
	size_t	pcmSize = 0;
	while(!ai->flagStop)
	{
		Sleep(ai->bufferDuration/2);
		HRESULT hr = ai->pCaptureClient->GetNextPacketSize(&packetLength);
		if(FAILED(hr))break;
		pcmSize = 0;
		bool err = false;
		while(packetLength != 0)
		{
			hr = ai->pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
			if(FAILED(hr)){err = true;break;}
			size_t len = numFramesAvailable*ai->nChannels*ai->nBitsPerSampleChannel/8;
			memcpy(&pcmBuffer[pcmSize], pData, len);
			pcmSize += len;
			hr = ai->pCaptureClient->ReleaseBuffer(numFramesAvailable);
			if(FAILED(hr)){err = true;break;}
			hr = ai->pCaptureClient->GetNextPacketSize(&packetLength);
			if(FAILED(hr)){err = true;break;}
		}
		if(err)break;
		if(pcmSize)
		{
			ai->onRecordImpl(pcmBuffer, pcmSize);
		}
	}
	ai->pAudioClient->Stop();
	delete[] pcmBuffer;
	return 0;
}

void AudioInput::stopDShowDevice()
{
	if(pGrabber)
	{
		pGrabber->SetCallback(NULL, 1);
	}
	if(sgCallback)
	{
		sgCallback->Release();
		delete sgCallback;
		sgCallback = 0;
	}
	if(pControl)
	{
		pControl->Pause();
		pControl->Stop();
	}
	if(pAudioInputFilter)
	{
		nukeDownstream(pAudioInputFilter);
	}
	if(pDestFilter)
	{
		pDestFilter->Release();
		pDestFilter = 0;
	}
	if(pAudioInputFilter)
	{
		pAudioInputFilter->Release();
		pAudioInputFilter = 0;
	}
	if(pGrabberF)
	{
		pGrabberF->Release();
		pGrabberF = 0;
	}
	if(pGrabber)
	{
		pGrabber->Release();
		pGrabber = 0;
	}
	if(pControl)
	{
		pControl->Release();
		pControl = 0;
	}
	if(pGraph)
	{
		destroyGraph();
	}
	if(pCaptureGraphBuilder)
	{
		pCaptureGraphBuilder->Release();
		pCaptureGraphBuilder = 0;
	}
	if(pGraph)
	{
		pGraph->Release();
		pGraph = 0;
	}
}

void AudioInput::nukeDownstream(IBaseFilter *pBF)
{
	IPin *pP, *pTo;
	ULONG u;
	IEnumPins *pins = NULL;
	PIN_INFO pininfo;
	HRESULT hr = pBF->EnumPins(&pins);
	pins->Reset();
	while (hr == NOERROR)
	{
		hr = pins->Next(1, &pP, &u);
		if (hr == S_OK && pP)
		{
			pP->ConnectedTo(&pTo);
			if (pTo)
			{
				hr = pTo->QueryPinInfo(&pininfo);
				if (hr == NOERROR)
				{
					if (pininfo.dir == PINDIR_INPUT)
					{
						nukeDownstream(pininfo.pFilter);
						pGraph->Disconnect(pTo);
						pGraph->Disconnect(pP);
						pGraph->RemoveFilter(pininfo.pFilter);
					}
					pininfo.pFilter->Release();
					pininfo.pFilter = NULL;
				}
				pTo->Release();
			}
			pP->Release();
		}
	}
	if (pins) pins->Release();
}

void AudioInput::onRecord(const void* pcm, size_t bytes)
{
}

void AudioInput::setCallback(AI_RECORD_CALLBACK cbfunc, void* userdata)
{
	recCallback = cbfunc;
	recUserdata = userdata;
}

void AudioInput::onRecordImpl(const void* pcm, size_t bytes)
{
	if(recCallback)
	{
		recCallback(pcm, bytes, recUserdata);
	}
	else
	{
		onRecord(pcm, bytes);
	}
}
