#pragma once

extern "C"
{
#include <libavformat/avformat.h>
}

struct AudioParams
{
   int            channelCount;
   AVSampleFormat sampleFormat;
   int            sampleRate;
   int            bytesPerSample;
};
