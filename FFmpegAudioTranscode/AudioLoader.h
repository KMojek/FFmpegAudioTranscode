#pragma once

#include "AudioParams.h"

#include <memory>
#include <string>
#include <vector>

extern "C"
{
   struct AVFrame;
}

class AudioReaderDecoder;
class AudioResampler;

enum class AudioReaderDecoderInitState;
enum class AudioResamplerInitState;

class AudioLoader
{
public:
   AudioLoader( const std::string& path, bool forceLittleEndian=false );
   virtual ~AudioLoader();

   enum State { Ok, NoInit, ReaderDecoderInitFails, ResamplerInitFails, LoadAudioFails };

   bool loadAudioData();

   State state() const { return _state; }
   bool readerDecoderInitState( AudioReaderDecoderInitState& state ) const;
   bool resamplerInitState( AudioResamplerInitState& state ) const;

   // 16-bit stereo interleaved audio samples
   const std::vector<int16_t> & processedAudio() const { return _processedAudio; }

protected:
   void processDecodedAudio( const AVFrame* );
   void copyResampledAudio( int sampleCount );
   void flushResampleBuffer();

   const std::string                   _path;
   const bool                          _forceLittleEndian;
   State                               _state;
   std::unique_ptr<AudioReaderDecoder> _readerDecoder;
   std::unique_ptr<AudioResampler>     _resampler;
   std::vector<int16_t>                _processedAudio;
   std::unique_ptr<uint8_t[]>          _resampleBuff;
   int                                 _numInResampleBuffer;
   int                                 _resampleBufferSampleCapacity;
   AudioParams                         _inputParams;
   AudioParams                         _resamplerInputParams;
   int                                 _primingAdjustment;
};
