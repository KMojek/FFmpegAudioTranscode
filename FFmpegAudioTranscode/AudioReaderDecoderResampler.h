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

class AudioReaderDecoderResampler
{
public:
   AudioReaderDecoderResampler( const std::string& path );
   virtual ~AudioReaderDecoderResampler();

   enum State { Ok, NoInit, ReaderDecoderInitFails, ResamplerInitFails, LoadAudioFails };

   bool loadAudioData();

   State state() const { return _state; }
   bool readerDecoderInitState( AudioReaderDecoderInitState& state ) const;
   bool resamplerInitState( AudioResamplerInitState& state ) const;

   const std::vector<int16_t> &  leftChannelData() const { return _leftChannel; }
   const std::vector<int16_t> &  rightChannelData() const { return _rightChannel; }

protected:
   void processDecodedAudio( const AVFrame* );
   void copyResampledAudio( int sampleCount );
   void flushResampleBuffer();

   const std::string                   _path;
   State                               _state;
   std::unique_ptr<AudioReaderDecoder> _readerDecoder;
   std::unique_ptr<AudioResampler>     _resampler;
   std::vector<int16_t>                _leftChannel;
   std::vector<int16_t>                _rightChannel;
   bool                                _isPlanar;
   std::unique_ptr<uint8_t[]>          _leftResampleBuff;
   std::unique_ptr<uint8_t[]>          _rightResampleBuff;
   std::unique_ptr<uint8_t[]>          _nonPlanarResampleBuff;
   int                                 _numInResampleBuffer;
   int                                 _resampleBufferSampleCapacity;
   AudioParams                         _inputParams;
   int                                 _primingAdjustment;
};
