#include "stdafx.h"

#include "AudioReaderDecoderResampler.h"
#include "AudioParams.h"
#include "AudioReaderDecoder.h"
#include "AudioResampler.h"

#include <algorithm>

#include <string.h>

AudioReaderDecoderResampler::AudioReaderDecoderResampler( const std::string& path )
   : _path( path )
   , _numInResampleBuffer( 0 )
   , _resampleBufferSampleCapacity( 0 )
   , _isPlanar( false )
{
#if LIBAVFORMAT_VERSION_MAJOR < 58
   ::avcodec_register_all();
   ::av_register_all();
#endif
}

AudioReaderDecoderResampler::~AudioReaderDecoderResampler()
{

}

bool AudioReaderDecoderResampler::loadAudioData()
{
   _readerDecoder.reset( new AudioReaderDecoder( _path ) );

   if ( _readerDecoder->initialize() != AudioReaderDecoder::Ok )
      return false;

   if ( !_readerDecoder->getAudioParams( _inputParams ) )
      return false;

   AudioParams outputParams = { 2, AV_SAMPLE_FMT_S16, 44100, 2 };

   _resampler.reset( new AudioResampler( _inputParams, _inputParams.sampleRate, outputParams ) );
   if ( _resampler->initialize() != AudioResampler::Ok )
      return false;

   _resampleBufferSampleCapacity = _inputParams.sampleRate;

   _isPlanar = ( ::av_sample_fmt_is_planar( _inputParams.sampleFormat ) != 0 );
   if ( _isPlanar )
   {
      int bufferSize = _resampleBufferSampleCapacity * _inputParams.bytesPerSample;

      _leftResampleBuff.reset( new uint8_t[bufferSize] );
      _rightResampleBuff.reset( new uint8_t[bufferSize] );
      ::memset( _leftResampleBuff.get(), 0, bufferSize );
      ::memset( _rightResampleBuff.get(), 0, bufferSize );
   }
   else
   {
      int bufferSize = _resampleBufferSampleCapacity * _inputParams.bytesPerSample * _inputParams.channelCount;
      _nonPlanarResampleBuff.reset( new uint8_t[bufferSize] );
      ::memset( _nonPlanarResampleBuff.get(), 0, bufferSize );
   }

   std::function< void(const AVFrame *) > callback = [this]( const AVFrame *frame )
   {
      this->processDecodedAudio( frame );
   };
   _readerDecoder->readAndDecode( callback );

   int numFlushed = _resampler->flush();
   if ( numFlushed > 0 )
   {
      auto output = _resampler->outputBuffers();
      const int16_t *ptr = (const int16_t *)output[0];
      for ( int i = 0; i < numFlushed; ++i )
      {
         _leftChannel.push_back( *ptr++ );
         _rightChannel.push_back( *ptr++ );
      }
   }

   return true;
}

void AudioReaderDecoderResampler::processDecodedAudio( const AVFrame* frame )
{
   int sampleCount = frame->nb_samples;
   int numToCopy = std::min( _resampleBufferSampleCapacity - _numInResampleBuffer, sampleCount );

   if ( _isPlanar && _inputParams.channelCount == 2 )
   {
      const uint8_t* leftCh = frame->data[0];
      const uint8_t* rightCh = frame->data[1];
      ::memcpy( _leftResampleBuff.get() + _numInResampleBuffer * _inputParams.bytesPerSample, leftCh, numToCopy * _inputParams.bytesPerSample );
      ::memcpy( _rightResampleBuff.get() + _numInResampleBuffer * _inputParams.bytesPerSample, rightCh, numToCopy * _inputParams.bytesPerSample );
      _numInResampleBuffer += numToCopy;

      if ( _numInResampleBuffer == _resampleBufferSampleCapacity )
      {
         int numConverted = _resampler->convert( _leftResampleBuff.get(), _rightResampleBuff.get(), _resampleBufferSampleCapacity );

         // hard-coded here for 16-bit stereo output
         auto output = _resampler->outputBuffers();
         const int16_t *ptr = (const int16_t *)output[0];
         for ( int i = 0; i < numConverted; ++i )
         {
            _leftChannel.push_back( *ptr++ );
            _rightChannel.push_back( *ptr++ );
         }

         ::memcpy( _leftResampleBuff.get(), leftCh + numToCopy * _inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.bytesPerSample );
         ::memcpy( _rightResampleBuff.get(), rightCh + numToCopy * _inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.bytesPerSample );

         _numInResampleBuffer = sampleCount - numToCopy;
      }
   }
   else if ( _isPlanar && _inputParams.channelCount == 1 )
   {
      // todo
   }
   else if ( !_isPlanar && _inputParams.channelCount <= 2 )
   {
      const uint8_t* inputCh = frame->data[0];
      ::memcpy( _nonPlanarResampleBuff.get() + _numInResampleBuffer * _inputParams.channelCount *_inputParams.bytesPerSample, inputCh, numToCopy * _inputParams.channelCount *_inputParams.bytesPerSample );
      _numInResampleBuffer += numToCopy;

      if ( _numInResampleBuffer == _resampleBufferSampleCapacity )
      {
         int numConverted = _resampler->convert( _nonPlanarResampleBuff.get(), _resampleBufferSampleCapacity );

         // hard-coded here for 16-bit stereo output
         auto output = _resampler->outputBuffers();
         const int16_t *ptr = (const int16_t *)output[0];
         for ( int i = 0; i < numConverted; ++i )
         {
            _leftChannel.push_back( *ptr++ );
            _rightChannel.push_back( *ptr++ );
         }

         ::memcpy( _nonPlanarResampleBuff.get(), inputCh + numToCopy * _inputParams.channelCount *_inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.channelCount *_inputParams.bytesPerSample );

         _numInResampleBuffer = sampleCount - numToCopy;
      }
   }
   else
   {
      // only supporting mono and stereo currently
   }

}
