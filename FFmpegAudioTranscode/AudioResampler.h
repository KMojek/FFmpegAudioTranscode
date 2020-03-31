#pragma once

#include "AudioParams.h"

#include <cstdint>

extern "C"
{
   struct SwrContext;
}

enum class AudioResamplerInitState
{
   Ok, NoInit, InitFails, OutputInitFails
};

class AudioResampler
{
public:
   AudioResampler( const AudioParams& inputParams, int inMaxSampleCount, const AudioParams& outputParams );
   virtual ~AudioResampler();

   AudioResamplerInitState initialize();
   AudioResamplerInitState initState() const { return _initState; }

   int convert( const uint8_t* leftPtr, const uint8_t* rightPtr, int n );
   int convert( const uint8_t* nonPlanarPtr, int n );
   int flush();

   int numConverted() const { return _numConverted; }
   const uint8_t * const * outputBuffers() const { return _dstData; }

protected:
   const AudioParams       _inputParams;
   const int               _maxInSampleCount;
   const AudioParams       _outputParams;
   int                     _maxReturnedSampleCount;
   uint8_t**               _dstData;
   AudioResamplerInitState _initState;
   SwrContext*             _swrContext;
   int                     _numConverted;
};
