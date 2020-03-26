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
{

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

   int bufferSize = _resampleBufferSampleCapacity * _inputParams.bytesPerSample;
   _leftResampleBuff.reset( new uint8_t[ bufferSize ] );
   _rightResampleBuff.reset( new uint8_t[ bufferSize ] );

   std::function< void(const AVFrame *) > callback = [this]( const AVFrame *frame ) { this->processDecodedAudio( frame ); };
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
   const uint8_t* leftCh = frame->data[0];
   const uint8_t* rightCh = frame->data[1];
   int sampleCount = frame->nb_samples;
   int numToCopy = std::min( _resampleBufferSampleCapacity - _numInResampleBuffer, sampleCount );
   ::memcpy( _leftResampleBuff.get() + _numInResampleBuffer * _inputParams.bytesPerSample, leftCh, numToCopy  * _inputParams.bytesPerSample );
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
