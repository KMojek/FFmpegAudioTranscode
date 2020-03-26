#pragma once

#include <memory>
#include <string>
#include <vector>

extern "C"
{
   struct AVFrame;
}

class AudioReaderDecoder;
class AudioResampler;

class AudioReaderDecoderResampler
{
public:
   AudioReaderDecoderResampler( const std::string& path );
   virtual ~AudioReaderDecoderResampler();

   bool loadAudioData();

   const std::vector<int16_t> &  leftChannelData() const { return _leftChannel; }
   const std::vector<int16_t> &  rightChannelData() const { return _rightChannel; }

protected:
   void processDecodedAudio( const AVFrame* );

   const std::string                   _path;
   std::unique_ptr<AudioReaderDecoder> _readerDecoder;
   std::unique_ptr<AudioResampler>     _resampler;
   std::vector<int16_t>                _leftChannel;
   std::vector<int16_t>                _rightChannel;
   std::unique_ptr<float[]>            _leftResampleBuff;
   std::unique_ptr<float[]>            _rightResampleBuff;
   int                                 _numInResampleBuffer;
   int                                 _resampleBufferSize;
};
