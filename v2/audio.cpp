#include<stdint.h>
#include<stdio.h>
#include<Windows.h>
#include<initguid.h>
#include<Audioclient.h>
#include<mmdeviceapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ole32.lib")
#endif

enum
{
	NCHANNELS=2,
};
typedef struct _AudioCtx
{
	void *threadctx;
	IMMDeviceEnumerator *enumerator;
	IMMDevice *device;
	IAudioClient *client;
	WAVEFORMATEX *mixFormat;
	REFERENCE_TIME bufferDuration;
	HANDLE hEvent;
	UINT32 bufferFrameCount;
	IAudioRenderClient *render;
	int32_t playing, stopflag;
	IAudioClock *clock;
} AudioCtx;
AudioCtx audioctx={0};
extern "C" void* mt_exec(void (*func)(void*), void *args, int argbytes, int nthreads);
extern "C" void  mt_finish(void *mt_ctx);
extern "C" int audioplayback_dequeue(float *out, int nsamples);

extern "C" double time_sec_audioclock(void)
{
	uint64_t t=0, freq=0;
	audioctx.clock->GetPosition(&t, 0);
	audioctx.clock->GetFrequency(&freq);
	return (double)t/freq;
}
extern "C" void audioplayback_thread(void *p)
{
	AudioCtx *ctx=(AudioCtx*)p;
	IAudioClient *client=ctx->client;
	IAudioRenderClient *render=ctx->render;
	UINT32 bufferFrameCount=ctx->bufferFrameCount;
	HANDLE hEvent=ctx->hEvent;

	while(!ctx->stopflag)
	{
		if(!ctx->playing)
		{
			//WaitForSingleObject(ctx->hEvent, 10);
			Sleep(10);
			continue;
		}

		WaitForSingleObject(hEvent, INFINITE);//Wait until WASAPI says we can write
		
		if(!ctx->playing)
			continue;
		if(ctx->stopflag)
			break;

		UINT32 padding = 0;
		client->GetCurrentPadding(&padding);

		UINT32 nframes=bufferFrameCount-padding;
		if(!nframes)
			continue;
		
		BYTE *pData=0;
		render->GetBuffer(nframes, &pData);//Request WASAPI buffer
		float *out=(float*)pData;

		int pulled=audioplayback_dequeue((float*)out, nframes);//Pull from circular buffer

		if(pulled<(int)nframes)//Handle underrun
			memset(out+pulled*NCHANNELS, 0, ((ptrdiff_t)nframes-pulled)*sizeof(float[NCHANNELS]));//Zero remaining samples (silence)

		render->ReleaseBuffer(nframes, 0);//Release WASAPI buffer
	}
}

typedef enum MessageBoxTypeEnum
{
	MBOX_OK,
	MBOX_OKCANCEL,
	MBOX_YESNOCANCEL,
} MessageBoxType;
extern "C" int messagebox(MessageBoxType type, const char *title, const char *format, ...);
static int audioplayback_error(int line, int e)
{
	messagebox(MBOX_OK, "Error", "audio.cpp(%d): %d", line, e);
	return e;
}
#define ERROR_A(E) audioplayback_error(__LINE__, E)
extern "C" int audioplayback_start(void)
{
	int ret=0;

	memset(&audioctx, 0, sizeof(audioctx));

	ret=CoInitializeEx(0, COINIT_MULTITHREADED);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.enumerator=0;
	ret=CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		0,
		CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&audioctx.enumerator
	);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.device=0;
	ret=audioctx.enumerator->GetDefaultAudioEndpoint(
		eRender,	// playback device
		eConsole,	// default role
		&audioctx.device
	);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.client=0;
	audioctx.device->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL,
		0,
		(void**)&audioctx.client
	);

	audioctx.mixFormat=0;
	ret=audioctx.client->GetMixFormat(&audioctx.mixFormat);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.bufferDuration=1000000;

	ret=audioctx.client->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
		audioctx.bufferDuration,
		0,
		audioctx.mixFormat,
		0
	);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.hEvent=CreateEvent(0, FALSE, FALSE, 0);
	ret=audioctx.client->SetEventHandle(audioctx.hEvent);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.bufferFrameCount=0;
	ret=audioctx.client->GetBufferSize(&audioctx.bufferFrameCount);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.render=0;
	ret=audioctx.client->GetService(
		__uuidof(IAudioRenderClient),
		(void**)&audioctx.render
	);
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.client->GetService(__uuidof(IAudioClock), (void**)&audioctx.clock);

	ret=audioctx.client->Start();
	if(ret!=S_OK)
	{
		ERROR_A(ret);
		return ret;
	}

	audioctx.playing=1;
	audioctx.threadctx=mt_exec(audioplayback_thread, &audioctx, sizeof(audioctx), 1);

	return ret;
}
extern "C" int audioplayback_pause(int stop)
{
	int ret=S_OK;

	if(stop)//end
	{
		ret=audioctx.client->Stop();
		if(ret!=S_OK)
			ERROR_A(ret);
		ret=audioctx.client->Reset();
		if(ret!=S_OK)
			ERROR_A(ret);
		audioctx.stopflag=1;
		mt_finish(audioctx.threadctx);
		CoUninitialize();
	}
	else//pause
	{
		audioctx.playing=1-audioctx.playing;//dumb cpp warnings
		if(audioctx.playing)
			ret=audioctx.client->Start();
		else
			ret=audioctx.client->Stop();
		if(ret!=S_OK)
			ERROR_A(ret);
	}
	return ret;
}