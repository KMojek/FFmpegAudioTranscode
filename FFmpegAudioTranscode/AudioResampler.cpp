#include "stdafx.h"

#include "AudioResampler.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

AudioResampler::AudioResampler( const AudioParams& inputParams, int maxInSampleCount, const AudioParams& outputParams )
   : _inputParams( inputParams )
   , _maxInSampleCount( maxInSampleCount )
   , _outputParams( outputParams )
   , _maxReturnedSampleCount( 0 )
   , _dstData( nullptr )
   , _initState( AudioResamplerInitState::NoInit )
   , _swrContext( nullptr )
   , _numConverted( 0 )
{

}

AudioResampler::~AudioResampler()
{
   if ( _dstData != nullptr )
   {
      int n = ( ::av_sample_fmt_is_planar( _outputParams.sampleFormat ) != 0 ) ? _outputParams.channelCount : 1;
      for ( int i = 0; i < n; ++i )
         ::av_freep( &_dstData[i] );
      ::av_freep( &_dstData );
   }

   if ( _swrContext != nullptr )
   {
      ::swr_close( _swrContext );
      ::swr_free( &_swrContext );
   }
}

#define SetStateAndReturn(a) \
{                  \
   _initState = a; \
   return a;       \
}

AudioResamplerInitState AudioResampler::initialize()
{
   if ( _initState != AudioResamplerInitState::NoInit )
      return _initState;

   uint64_t inChannelLayout = ::av_get_default_channel_layout( _inputParams.channelCount );
   uint64_t outChannelLayout = ::av_get_default_channel_layout( _outputParams.channelCount );

   _swrContext = ::swr_alloc_set_opts( nullptr,
                                       outChannelLayout, _outputParams.sampleFormat, _outputParams.sampleRate,
                                       inChannelLayout, _inputParams.sampleFormat, _inputParams.sampleRate,
                                       0, nullptr );

   ::swr_init( _swrContext );
   if ( ::swr_is_initialized( _swrContext ) == 0 )
      SetStateAndReturn( AudioResamplerInitState::InitFails );

   _maxReturnedSampleCount = ::swr_get_out_samples( _swrContext, _maxInSampleCount );

   int dst_linesize = 0;
   int status = ::av_samples_alloc_array_and_samples( &_dstData, &dst_linesize, _outputParams.channelCount, _maxReturnedSampleCount, _outputParams.sampleFormat, 0 );
   if ( status <= 0 )
      SetStateAndReturn( AudioResamplerInitState::OutputInitFails );

   SetStateAndReturn( AudioResamplerInitState::Ok );
}

int AudioResampler::convert( const uint8_t *nonPlanarPtr, int n )
{
   if ( _initState == AudioResamplerInitState::NoInit )
      initialize();
   if ( _initState != AudioResamplerInitState::Ok )
      return 0;

   return ::swr_convert( _swrContext, _dstData, _maxReturnedSampleCount, &nonPlanarPtr, n );
}

int AudioResampler::flush()
{
   return ::swr_convert( _swrContext, _dstData, _maxReturnedSampleCount, nullptr, 0 );
}
