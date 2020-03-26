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
   , _resampleBufferSize( 0 )
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

   AudioParams inputParams;
   if ( !_readerDecoder->getAudioParams( inputParams ) )
      return false;

   AudioParams outputParams = { 2, AV_SAMPLE_FMT_S16, 44100, 2 };

   _resampler.reset( new AudioResampler( inputParams, inputParams.sampleRate, outputParams ) );
   if ( _resampler->initialize() != AudioResampler::Ok )
      return false;

   _resampleBufferSize = inputParams.sampleRate;
   _leftResampleBuff.reset( new float[_resampleBufferSize] );
   _rightResampleBuff.reset( new float[_resampleBufferSize] );

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
   int numToCopy = std::min( _resampleBufferSize - _numInResampleBuffer, sampleCount );
   ::memcpy( _leftResampleBuff.get() + _numInResampleBuffer, leftCh, numToCopy * sizeof( float ) );
   ::memcpy( _rightResampleBuff.get() + _numInResampleBuffer, rightCh, numToCopy * sizeof( float ) );
   _numInResampleBuffer += numToCopy;

   if ( _numInResampleBuffer == _resampleBufferSize )
   {
      int numConverted = _resampler->convert( _leftResampleBuff.get(), _rightResampleBuff.get(), _resampleBufferSize );
      auto output = _resampler->outputBuffers();
      const int16_t *ptr = (const int16_t *)output[0];
      for ( int i = 0; i < numConverted; ++i )
      {
         _leftChannel.push_back( *ptr++ );
         _rightChannel.push_back( *ptr++ );
      }

      ::memcpy( _leftResampleBuff.get(), leftCh + numToCopy * sizeof( float ), ( sampleCount - numToCopy ) * sizeof( float ) );
      ::memcpy( _rightResampleBuff.get(), rightCh + numToCopy * sizeof( float ), ( sampleCount - numToCopy ) * sizeof( float ) );

      _numInResampleBuffer = sampleCount - numToCopy;
   }
}
