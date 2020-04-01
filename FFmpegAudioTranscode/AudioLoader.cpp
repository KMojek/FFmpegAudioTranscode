#include "stdafx.h"

#include "AudioLoader.h"
#include "AudioParams.h"
#include "AudioReaderDecoder.h"
#include "AudioResampler.h"

#include <algorithm>

#include <string.h>

namespace
{
   int16_t swap_endian( int16_t s )
   {
      int8_t *ch = (int8_t*)&s;
      std::swap( ch[0], ch[1] );
      return *(int16_t *)ch;
   }
}

AudioLoader::AudioLoader( const std::string& path, bool forceLittleEndian/*=false*/ )
   : _path( path )
   , _forceLittleEndian( forceLittleEndian )
   , _state( NoInit )
   , _numInResampleBuffer( 0 )
   , _resampleBufferSampleCapacity( 0 )
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

AudioLoader::~AudioLoader()
{

}

#define SetStateAndReturn(a, b) \
{              \
   _state = a; \
   return b;   \
}

bool AudioLoader::loadAudioData()
{
   _readerDecoder.reset( new AudioReaderDecoder( _path ) );

   if ( _readerDecoder->initialize() != AudioReaderDecoderInitState::Ok )
      SetStateAndReturn( ReaderDecoderInitFails, false );

   // ReaderDecoder has already successfully initialized so no need to check return value
   _readerDecoder->getAudioParams( _inputParams );

   // We always feed the resampler with interleaved (aka packed) data
   _resamplerInputParams = _inputParams;
   _resamplerInputParams.sampleFormat = ::av_get_packed_sample_fmt( _inputParams.sampleFormat );

   AudioParams outputParams = { 2, AV_SAMPLE_FMT_S16, 44100, 2 };

   _resampler.reset( new AudioResampler( _resamplerInputParams, _resamplerInputParams.sampleRate, outputParams ) );
   if ( _resampler->initialize() != AudioResamplerInitState::Ok )
      SetStateAndReturn( ResamplerInitFails, false );

   _resampleBufferSampleCapacity = _inputParams.sampleRate;

   int bufferSize = _resampleBufferSampleCapacity * _inputParams.channelCount *_inputParams.bytesPerSample;

   _resampleBuff.reset( new uint8_t[bufferSize] );
   ::memset( _resampleBuff.get(), 0, bufferSize );

   std::function< void( const AVFrame * ) > callback = [this]( const AVFrame *frame )
   {
      this->processDecodedAudio( frame );
   };

   if ( !_readerDecoder->readAndDecode( callback ) )
      SetStateAndReturn( LoadAudioFails, false );

   flushResampleBuffer();

   int numFlushed = _resampler->flush();
   if ( numFlushed > 0 )
      copyResampledAudio( numFlushed );

   if ( _forceLittleEndian )
   {
      int i = 1;
      char c = *(char *)&i;
      if ( c == 0 ) // running on big-endian architecture
      {
         for ( auto iter = _processedAudio.begin(); iter != _processedAudio.end(); ++iter )
            *iter = swap_endian( *iter );
      }
   }

   SetStateAndReturn( Ok, true );
}

bool AudioLoader::readerDecoderInitState( AudioReaderDecoderInitState& state ) const
{
   if ( _readerDecoder == nullptr )
      return false;

   state = _readerDecoder->initState();
   return true;
}

bool AudioLoader::resamplerInitState( AudioResamplerInitState& state ) const
{
   if ( _resampler == nullptr )
      return false;

   state = _resampler->initState();
   return true;
}

void AudioLoader::processDecodedAudio( const AVFrame* frame )
{
   int sampleCount = frame->nb_samples;
   int numToCopy = std::min( _resampleBufferSampleCapacity - _numInResampleBuffer, sampleCount );
   bool needToInterleaveSamples = ( _inputParams.sampleFormat != _resamplerInputParams.sampleFormat );
   int n = _inputParams.channelCount * _inputParams.bytesPerSample;

   if ( !needToInterleaveSamples )
   {
      ::memcpy( _resampleBuff.get() + _numInResampleBuffer * n, frame->data[0], numToCopy * n );
   }
   else
   {
      uint8_t* dst = _resampleBuff.get() + _numInResampleBuffer * n;
      for ( int i = 0; i < numToCopy; ++i )
      {
         for ( int ii = 0; ii < _inputParams.channelCount; ++ii )
         {
            const uint8_t* src = &frame->data[ii][i * _inputParams.bytesPerSample];
            ::memcpy( dst, src, _inputParams.bytesPerSample );
            dst += _inputParams.bytesPerSample;
         }
      }
   }
   _numInResampleBuffer += numToCopy;

   // Resample buffer was filled... need to resample and preserve leftovers from this frame
   if ( _numInResampleBuffer == _resampleBufferSampleCapacity )
   {
      int numConverted = _resampler->convert( _resampleBuff.get(), _resampleBufferSampleCapacity );
      int numLeftovers = sampleCount - numToCopy;

      copyResampledAudio( numConverted );

      if ( !needToInterleaveSamples )
      {
         ::memcpy( _resampleBuff.get(), frame->data[0] + numToCopy * n, numLeftovers * n );
      }
      else
      {
         int srcStartIndex = numToCopy * _inputParams.bytesPerSample;
         uint8_t* dst = _resampleBuff.get();
         for ( int i = 0; i < numLeftovers; ++i )
         {
            for ( int ii = 0; ii < _inputParams.channelCount; ++ii )
            {
               const uint8_t* src = &frame->data[ii][srcStartIndex + i * _inputParams.bytesPerSample];
               ::memcpy( dst, src, _inputParams.bytesPerSample );
               dst += _inputParams.bytesPerSample;
            }
         }
      }

      _numInResampleBuffer = sampleCount - numToCopy;
   }
}

void AudioLoader::copyResampledAudio( int sampleCount )
{
   auto output = _resampler->outputBuffers();
   const int16_t *ptr = (const int16_t *)output[0];
   for ( int i = 0; i < sampleCount; ++i )
   {
      _processedAudio.push_back( *ptr++ );
      _processedAudio.push_back( *ptr++ );
   }
}

void AudioLoader::flushResampleBuffer()
{
   if ( _numInResampleBuffer == 0 )
      return;

   int numConverted = _resampler->convert( _resampleBuff.get(), std::min( _numInResampleBuffer + _primingAdjustment, _resampleBufferSampleCapacity ) );
   copyResampledAudio( numConverted );

   _numInResampleBuffer = 0;
}
