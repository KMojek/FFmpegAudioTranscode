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

   AudioParams()
      : channelCount(0), sampleFormat( AV_SAMPLE_FMT_NONE ), sampleRate( 0 ), bytesPerSample( 0 ) {}
   AudioParams( int i_cc, AVSampleFormat i_sf, int i_sampleRate, int i_bytesPerSample )
      : channelCount( i_cc ), sampleFormat( i_sf ), sampleRate( i_sampleRate ), bytesPerSample( i_bytesPerSample ) {}
};
