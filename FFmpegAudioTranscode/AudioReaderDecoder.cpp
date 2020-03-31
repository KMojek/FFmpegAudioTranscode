#include "stdafx.h"

#include "AudioReaderDecoder.h"
#include "AudioParams.h"
#include "WavUtil.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

AudioReaderDecoder::AudioReaderDecoder( const std::string& path )
   : _path( path )
   , _initState( AudioReaderDecoderInitState::NoInit )
   , _streamIndex( -1 )
   , _formatContext( nullptr )
   , _codecContext( nullptr )
   , _packet( nullptr )
   , _frame( nullptr )
{

}

AudioReaderDecoder::~AudioReaderDecoder()
{
   if ( _frame != nullptr )
      ::av_frame_free( &_frame );
   if ( _packet != nullptr )
      ::av_packet_free( &_packet );
   if ( _codecContext != nullptr )
      ::avcodec_free_context( &_codecContext );
   if ( _formatContext != nullptr )
      ::avformat_free_context( _formatContext );
}

#define SetStateAndReturn(a) \
{                  \
   _initState = a; \
   return a;       \
}

AudioReaderDecoderInitState AudioReaderDecoder::initialize()
{
   if ( _initState != AudioReaderDecoderInitState::NoInit )
      return _initState;

   _formatContext = ::avformat_alloc_context();
   if ( _formatContext == nullptr )
      SetStateAndReturn( AudioReaderDecoderInitState::FormatContextAllocFails );

   int status = ::avformat_open_input( &_formatContext, _path.c_str(), nullptr, nullptr );
   if ( status != 0 )
      SetStateAndReturn( AudioReaderDecoderInitState::OpenFails );

   status = ::avformat_find_stream_info( _formatContext, nullptr );
   if ( status < 0 )
      SetStateAndReturn( AudioReaderDecoderInitState::FindStreamInfoFails );

   AVCodec *codec = nullptr;
   _streamIndex = ::av_find_best_stream( _formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0 );
   if ( _streamIndex == -1 )
      SetStateAndReturn( AudioReaderDecoderInitState::NoAudioStream );

   _codecContext = ::avcodec_alloc_context3( codec );
   if ( _codecContext == nullptr )
      SetStateAndReturn( AudioReaderDecoderInitState::CodecContextAllocFails );

   // workaround for WAV decoding bug
   if ( _formatContext->streams[_streamIndex]->codecpar->codec_id == AV_CODEC_ID_FIRST_AUDIO )
   {
      AudioParams params;
      ReadWavAudioParams( _path, params );
      _codecContext->sample_rate = params.sampleRate;
      _codecContext->sample_fmt = params.sampleFormat;
      _codecContext->channels = params.channelCount;
      _codecContext->channel_layout = ( params.channelCount == 1 ) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
   }

   status = ::avcodec_open2( _codecContext, codec, nullptr );
   if ( status != 0 )
      SetStateAndReturn( AudioReaderDecoderInitState::CodecOpenFails );

   _packet = ::av_packet_alloc();
   if ( _packet == nullptr )
      SetStateAndReturn( AudioReaderDecoderInitState::PacketAllocFails );
   ::av_init_packet( _packet );

   _frame = ::av_frame_alloc();
   if ( _frame == nullptr )
      SetStateAndReturn( AudioReaderDecoderInitState::FrameAllocFails );

   ::av_seek_frame( _formatContext, _streamIndex, 0, AVSEEK_FLAG_ANY );

   SetStateAndReturn( AudioReaderDecoderInitState::Ok );
}

bool AudioReaderDecoder::getAudioParams( AudioParams& p )
{
   if ( _initState == AudioReaderDecoderInitState::NoInit )
      initialize();

   if ( _initState != AudioReaderDecoderInitState::Ok )
      return false;

   p.sampleFormat = _codecContext->sample_fmt;

   const AVCodecParameters* codecParams = _formatContext->streams[_streamIndex]->codecpar;
   p.channelCount = codecParams->channels;
   p.sampleRate = codecParams->sample_rate;
   p.bytesPerSample = ::av_get_bytes_per_sample( _codecContext->sample_fmt );

   return true;
}

bool AudioReaderDecoder::readAndDecode( std::function<void( const AVFrame * )> callback )
{
   if ( _initState == AudioReaderDecoderInitState::NoInit )
      initialize();

   if ( _initState != AudioReaderDecoderInitState::Ok )
      return false;

   int status;
   for ( bool receivedEOF = false; !receivedEOF; )
   {
      while ( ( status = ::av_read_frame( _formatContext, _packet ) ) == 0 )
      {
         if ( _packet->stream_index == _streamIndex )
            break;
         ::av_packet_unref( _packet );
      }

      if ( status == AVERROR_EOF )
         receivedEOF = true;

      status = ::avcodec_send_packet( _codecContext, receivedEOF ? nullptr : _packet );

      if ( status == 0 )
      {
         do
         {
            status = ::avcodec_receive_frame( _codecContext, _frame );
            if ( status == AVERROR_EOF )
               break;

            if ( status == 0 )
               callback( _frame );
         } while ( status != AVERROR( EAGAIN ) );
      }
      ::av_packet_unref( _packet );
   }

   return true;
}