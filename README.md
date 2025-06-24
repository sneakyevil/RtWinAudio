# Basic Usage

```cpp
#include "rtwinaudio.hh"

int main()
{
    RtWinAudio rtAudio;
    rtAudio.Start();

    // Main loop
    bool done = 0;
    while (!done)
    {
        UINT32 numFrames;
        if (auto buffer = static_cast<float*>(rtAudio.GetBuffer(numFrames)))
        {
            int numChannels = rtAudio.GetNumChannels();
            
            for (UINT32 i = 0; numFrames > i; ++i)
            {
                int index = (i * numChannels);
                float mono_peak = 0.f;
                for (int c = 0; numChannels > c; ++c) {
                    mono_peak += buffer[index + c];
                }
                mono_peak /= static_cast<float>(numChannels);   

                // You can easily visualize the audio frame peak with this.
            }
        }
    }

    rtAudio.Stop();

    return 0;
}
```