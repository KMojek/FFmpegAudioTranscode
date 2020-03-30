#include "stdafx.h"

#include "AudioReaderDecoderResampler.h"
#include "AudioParams.h"
#include "AudioReaderDecoder.h"
#include "AudioResampler.h"

#include <algorithm>

#include <string.h>

AudioReaderDecoderResampler::AudioReaderDecoderResampler( const std::string& path )
   : _path( path )
   , _state( NoInit )
   , _numInResampleBuffer( 0 )
   , _resampleBufferSampleCapacity( 0 )
   , _isPlanar( false )
   , _primingAdjustment( 0 )
{
   // format-specific adjustment for "priming samples"
   size_t pos;
   if ( ( pos = path.rfind( '.' ) ) != std::string::npos )
   {
      std::string ext( path.substr( pos ) );
      if ( ext == ".mp3" )
         _primingAdjustment = 1152;
   }
}

AudioReaderDecoderResampler::~AudioReaderDecoderResampler()
{

}

#define SetStateAndReturn(a, b) \
{              \
   _state = a; \
   return b;   \
}

bool AudioReaderDecoderResampler::loadAudioData()
{
   _readerDecoder.reset( new AudioReaderDecoder( _path ) );

   if ( _readerDecoder->initialize() != AudioReaderDecoderInitState::Ok )
      SetStateAndReturn( ReaderDecoderInitFails, false );

   // ReaderDecoder has already successfully initialized so no need to check return value
   _readerDecoder->getAudioParams( _inputParams );

   AudioParams outputParams = { 2, AV_SAMPLE_FMT_S16, 44100, 2 };

   _resampler.reset( new AudioResampler( _inputParams, _inputParams.sampleRate, outputParams ) );
   if ( _resampler->initialize() != AudioResamplerInitState::Ok )
      SetStateAndReturn( ResamplerInitFails, false );

   _resampleBufferSampleCapacity = _inputParams.sampleRate;

   _isPlanar = ( ::av_sample_fmt_is_planar( _inputParams.sampleFormat ) != 0 );
   if ( _isPlanar )
   {
      int bufferSize = _resampleBufferSampleCapacity * _inputParams.bytesPerSample;

      if ( _inputParams.channelCount == 1 )
      {
         _nonPlanarResampleBuff.reset( new uint8_t[bufferSize] );
         ::memset( _nonPlanarResampleBuff.get(), 0, bufferSize );
      }
      else
      {
         _leftResampleBuff.reset( new uint8_t[bufferSize] );
         _rightResampleBuff.reset( new uint8_t[bufferSize] );
         ::memset( _leftResampleBuff.get(), 0, bufferSize );
         ::memset( _rightResampleBuff.get(), 0, bufferSize );
      }
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

   if ( !_readerDecoder->readAndDecode( callback ) )
      SetStateAndReturn( LoadAudioFails, false );

   flushResampleBuffer();

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

   SetStateAndReturn( Ok, true );
}

bool AudioReaderDecoderResampler::readerDecoderInitState( AudioReaderDecoderInitState& state ) const
{
   if ( _readerDecoder == nullptr )
      return false;

   state = _readerDecoder->initState();
   return true;
}

 bool AudioReaderDecoderResampler::resamplerInitState( AudioResamplerInitState& state ) const
{
    if ( _resampler == nullptr )
       return false;

    state = _resampler->initState();
    return true;
}

void AudioReaderDecoderResampler::processDecodedAudio( const AVFrame* frame )
{
   // For now, only support mono & stereo... silently fail otherwise
   if ( _inputParams.channelCount > 2 )
      return;

   int sampleCount = frame->nb_samples;
   int numToCopy = std::min( _resampleBufferSampleCapacity - _numInResampleBuffer, sampleCount );

   if ( _isPlanar )
   {
      if ( _inputParams.channelCount == 2 )
      {
         const uint8_t* leftCh = frame->data[0];
         const uint8_t* rightCh = frame->data[1];
         ::memcpy( _leftResampleBuff.get() + _numInResampleBuffer * _inputParams.bytesPerSample, leftCh, numToCopy * _inputParams.bytesPerSample );
         ::memcpy( _rightResampleBuff.get() + _numInResampleBuffer * _inputParams.bytesPerSample, rightCh, numToCopy * _inputParams.bytesPerSample );
         _numInResampleBuffer += numToCopy;

         if ( _numInResampleBuffer == _resampleBufferSampleCapacity )
         {
            int numConverted = _resampler->convert( _leftResampleBuff.get(), _rightResampleBuff.get(), _resampleBufferSampleCapacity );
            copyResampledAudio( numConverted );

            ::memcpy( _leftResampleBuff.get(), leftCh + numToCopy * _inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.bytesPerSample );
            ::memcpy( _rightResampleBuff.get(), rightCh + numToCopy * _inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.bytesPerSample );

            _numInResampleBuffer = sampleCount - numToCopy;
         }
      }
      else if ( _inputParams.channelCount == 1 )
      {
         const uint8_t* inputCh = frame->data[0];
         ::memcpy( _nonPlanarResampleBuff.get() + _numInResampleBuffer *_inputParams.bytesPerSample, inputCh, numToCopy *_inputParams.bytesPerSample );
         _numInResampleBuffer += numToCopy;

         if ( _numInResampleBuffer == _resampleBufferSampleCapacity )
         {
            int numConverted = _resampler->convert( _nonPlanarResampleBuff.get(), _resampleBufferSampleCapacity );
            copyResampledAudio( numConverted );

            ::memcpy( _nonPlanarResampleBuff.get(), inputCh + numToCopy *_inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.bytesPerSample );

            _numInResampleBuffer = sampleCount - numToCopy;
         }
      }
   }
   else if ( _inputParams.channelCount <= 2 )
   {
      const uint8_t* inputCh = frame->data[0];
      ::memcpy( _nonPlanarResampleBuff.get() + _numInResampleBuffer * _inputParams.channelCount *_inputParams.bytesPerSample, inputCh, numToCopy * _inputParams.channelCount *_inputParams.bytesPerSample );
      _numInResampleBuffer += numToCopy;

      if ( _numInResampleBuffer == _resampleBufferSampleCapacity )
      {
         int numConverted = _resampler->convert( _nonPlanarResampleBuff.get(), _resampleBufferSampleCapacity );
         copyResampledAudio( numConverted );

         ::memcpy( _nonPlanarResampleBuff.get(), inputCh + numToCopy * _inputParams.channelCount *_inputParams.bytesPerSample, ( sampleCount - numToCopy ) * _inputParams.channelCount *_inputParams.bytesPerSample );

         _numInResampleBuffer = sampleCount - numToCopy;
      }
   }
}

void AudioReaderDecoderResampler::copyResampledAudio( int sampleCount )
{
   auto output = _resampler->outputBuffers();
   const int16_t *ptr = (const int16_t *)output[0];
   for ( int i = 0; i < sampleCount; ++i )
   {
      _leftChannel.push_back( *ptr++ );
      _rightChannel.push_back( *ptr++ );
   }
}

void AudioReaderDecoderResampler::flushResampleBuffer()
{
   if ( _numInResampleBuffer == 0 )
      return;

   if ( _nonPlanarResampleBuff != nullptr )
   {
      int numConverted = _resampler->convert( _nonPlanarResampleBuff.get(),  std::min( _numInResampleBuffer + _primingAdjustment, _resampleBufferSampleCapacity ) );
      copyResampledAudio( numConverted );
   }
   else if ( _leftResampleBuff != nullptr && _rightResampleBuff != nullptr )
   {
      int numConverted = _resampler->convert( _leftResampleBuff.get(), _rightResampleBuff.get(), std::min( _numInResampleBuffer + _primingAdjustment, _resampleBufferSampleCapacity ) );
      copyResampledAudio( numConverted );
   }

   _numInResampleBuffer = 0;
}
