#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "mmdevapi.lib")

class RtWinAudio
{
private:
    IMMDevice* mDevice = 0;
    IAudioClient* mAudioClient = 0;
    IAudioCaptureClient* mCaptureClient = 0;

    WAVEFORMATEX* mWFX = 0;

    void* mBuffer = 0;
    int mBufferSize = 0;

    bool mCoInitialized = 0;

public:
    bool IsStarted() { return mCaptureClient; }

    bool FillBuffer(void* data, int frames)
    {
        int size = mWFX->nBlockAlign * frames;
        if (size > mBufferSize)
        {
            auto buf = realloc(mBuffer, size);
            if (!buf) {
                return 0;
            }

            mBuffer = buf;
        }
        
        memcpy(mBuffer, data, size);
        return 1;
    }

    void Release()
    {
        if (mWFX)
        {
            CoTaskMemFree(mWFX);
            mWFX = 0;
        }

        if (mDevice)
        {
            mDevice->Release();
            mDevice = 0;
        }

        if (mAudioClient)
        {
            mAudioClient->Stop();
            mAudioClient->Release();
            mAudioClient = 0;
        }

        if (mCaptureClient)
        {
            mCaptureClient->Release();
            mCaptureClient = 0;
        }

        if (mBuffer)
        {
            free(mBuffer);

            mBuffer = 0;
            mBufferSize = 0;
        }

        if (mCoInitialized)
        {
            CoUninitialize();
            mCoInitialized = 0;
        }
    }

	bool Start()
	{
        Release();

        mCoInitialized = (CoInitialize(0) == S_OK);

        IMMDeviceEnumerator* enumator = 0;
        HRESULT hres;

        hres = CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumator);
        if (FAILED(hres)) {
            return 0;
        }

        hres = enumator->GetDefaultAudioEndpoint(eRender, eConsole, &mDevice);
        enumator->Release();

        if (FAILED(hres)) {
            return 0;
        }

        hres = mDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, 0, (void**)&mAudioClient);
        if (FAILED(hres)) {
            return 0;
        }

        hres = mAudioClient->GetMixFormat(&mWFX);
        if (FAILED(hres)) {
            return 0;
        }

        hres = mAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mWFX, 0);
        if (FAILED(hres)) {
            return 0;
        }

        hres = mAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&mCaptureClient);
        if (FAILED(hres)) {
            return 0;
        }

        hres = mAudioClient->Start();
        if (FAILED(hres)) {
            return 0;
        }

        return 1;
	}

    void Stop()
    {
        Release();
    }

    // Gets latest buffer with most frames.
    void* GetBuffer(UINT32& numFrames)
    {
        numFrames = 0;

        BYTE* data;
        UINT32 availableFrames;
        DWORD flags;

        UINT32 packetSize;
        if (SUCCEEDED(mCaptureClient->GetNextPacketSize(&packetSize)) && packetSize > 0)
        {
            while (SUCCEEDED(mCaptureClient->GetBuffer(&data, &availableFrames, &flags, 0, 0)))
            {
                if (availableFrames > numFrames && FillBuffer(data, availableFrames)) {
                    numFrames = availableFrames;
                }

                mCaptureClient->GetNextPacketSize(&packetSize);
                mCaptureClient->ReleaseBuffer(availableFrames);

                if (packetSize <= 0) {
                    break;
                }
            }
        }

        if (numFrames == 0) {
            return 0;
        }

        return mBuffer;
    }

    int GetNumChannels() { return (mWFX ? mWFX->nChannels : 0); }
};