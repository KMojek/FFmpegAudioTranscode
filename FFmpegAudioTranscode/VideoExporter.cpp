#include "stdafx.h"

#include "VideoExporter.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef min
#undef min
#endif

namespace
{
   // initialize to solid-red
   bool getVideo( uint8_t* buf, int bufSize, unsigned /*frameIndex*/ )
   {
      uint8_t* ptr = buf;
      for ( int i = 0; i < bufSize; ++i, ++ptr )
         *ptr = ( i % 3 == 0 ) ? 0xff : 0x00;

      return true;
   }

   // initialize to silence
   bool getAudio( float* leftCh, float *rightCh, int frameSize )
   {
      std::memset( leftCh, 0, frameSize * sizeof(float) );
      std::memset( rightCh, 0, frameSize * sizeof(float) );

      return true;
   }

   void pushBufferedData( AVPacket* pkt, AVCodecContext* cc, AVFormatContext* fc )
   {
      int status = ::avcodec_send_frame( cc, nullptr );
      while ( 1 )
      {
         status = ::avcodec_receive_packet( cc, pkt );
         if ( status == 0 )
            status = ::av_interleaved_write_frame( fc, pkt );
         else if ( status == AVERROR_EOF )
            break;
      }
   }

   void update( const float **l, const float** r, int *count, int n )
   {
      *l += n;
      *r += n;
      *count -= n;
   }
}

VideoExporter::AudioAccumulator::AudioAccumulator( AVFrame* frame, int frameSize, std::function< void() > frameReadyCallback )
   : _frame( frame )
   , _frameSize( frameSize )
   , _frameReadyCallback( frameReadyCallback )
{
   if ( _frame->buf[0]->data == nullptr || _frame->buf[1]->data == nullptr )
      throw std::runtime_error( "VideoExporter - invalid audio input format" );

   _frame->nb_samples = 0;
}

void VideoExporter::AudioAccumulator::pushAudio( const float* leftCh, const float* rightCh, int sampleCount )
{
   while ( sampleCount > 0 )
   {
      int numProcessed = handlePartialOrCompletedFrame( leftCh, rightCh, sampleCount );
      update( &leftCh, &rightCh, &sampleCount, numProcessed );

      int numLeftover = std::min( _frameSize - numProcessed, sampleCount );
      if ( numLeftover > 0 )
      {
         std::memcpy( _frame->buf[0]->data, leftCh, numLeftover * sizeof( float ) );
         std::memcpy( _frame->buf[1]->data, rightCh, numLeftover * sizeof( float ) );
         _frame->nb_samples = numLeftover;

         update( &leftCh, &rightCh, &sampleCount, numLeftover );
      }
   }
}

int VideoExporter::AudioAccumulator::handlePartialOrCompletedFrame( const float* leftCh, const float* rightCh, int sampleCount )
{
   int numToCopy = std::min( _frameSize - _frame->nb_samples, sampleCount );

   float *dstLeft = reinterpret_cast<float *>( _frame->buf[0]->data );
   dstLeft += _frame->nb_samples;
   std::memcpy( dstLeft, leftCh, numToCopy * sizeof( float ) );

   float *dstRight = reinterpret_cast<float *>( _frame->buf[1]->data );
   dstRight += _frame->nb_samples;
   std::memcpy( dstRight, rightCh, numToCopy * sizeof( float ) );

   _frame->nb_samples += numToCopy;

   if ( _frame->nb_samples == _frameSize )
   {
      _frameReadyCallback();
      _frame->nb_samples = 0;
   }

   return numToCopy;
}


#define STUPID_SIMPLE

#ifdef STUPID_SIMPLE

VideoExporter::VideoExporter( const std::string& outPath, const Params& inParams, uint32_t frameCount )
   : _path( outPath )
   , _inParams( inParams )
   , _frameCount( frameCount )
{
   if ( inParams.pfmt != AV_PIX_FMT_RGB24 )
      throw std::runtime_error( "VideoExporter - expecting RGB24 input!" );

   _outParams = inParams;

   // MP4/MOV has some restrictions on width... apparently it's common
   // with FFmpeg to just enforce even-number width and height
   if ( _outParams.width % 2 )
      ++_outParams.width;
   if ( _outParams.height % 2 )
      ++_outParams.height;

   // We're outputing an H.264 / AAC MP4 file; most players only support profiles with 4:2:0 compression
   _outParams.pfmt = AV_PIX_FMT_YUV420P;

   _getVideo = getVideo;
   _getAudio = getAudio;
}

VideoExporter::~VideoExporter()
{
   cleanup();
}

void VideoExporter::initialize()
{
   // Initialize color-converter
   int sws_flags = SWS_FAST_BILINEAR; // usually doing just a colorspace conversion, so not too critical

   SwsContext* sws_ctx = ::sws_getContext( _inParams.width, _inParams.height, _inParams.pfmt,
                                           _outParams.width, _outParams.height, _outParams.pfmt,
                                           sws_flags, nullptr, nullptr, nullptr );
   if ( sws_ctx == nullptr )
      throw std::runtime_error( "VideoExporter - error setting up video format conversion!" );

   // Initialize video & audio
   AVOutputFormat* fmt = ::av_guess_format( nullptr, _path.c_str(), nullptr );
   const AVCodec* videoCodec = ::avcodec_find_encoder( fmt->video_codec );
   const AVCodec* audioCodec = ::avcodec_find_encoder( fmt->audio_codec );

   int status = ::avformat_alloc_output_context2( &_formatContext, fmt, nullptr, _path.c_str() );
   if ( _formatContext == nullptr )
      throw std::runtime_error( "VideoExporter - Error allocating output-context" );

   initializeVideo( videoCodec );
   initializeAudio( audioCodec );

   // Initialize frames and packets
   initializeFrames();
   initializePackets();

   // Open file for output and write header
   status = ::avio_open( &_formatContext->pb, _path.c_str(), AVIO_FLAG_WRITE );
   if ( status < 0 )
      throw std::runtime_error( "VideoExporter - Error opening output file" );

   // prepare to write... don't trust ::avformat_init_output() telling you that
   // a call to ::avformat_write_header() is unnecessary. If you don't call it,
   // the stream(s) won't be packaged in an MP4 container. Also, the stream's
   // time_base appears to be updated within this call.
   status = ::avformat_write_header( _formatContext, nullptr );
   if ( status < 0 )
      throw std::runtime_error( "VideoExporter - Error writing file header" );

   _ptsIncrement = _formatContext->streams[0]->time_base.den / _outParams.fps;
}

void VideoExporter::initializeVideo( const AVCodec* codec )
{
   AVStream* video_st = ::avformat_new_stream( _formatContext, nullptr );
   video_st->time_base.num = 1;
   video_st->time_base.den = _outParams.fps;

   AVCodecParameters* video_params = video_st->codecpar;
   video_params->width = _outParams.width;
   video_params->height = _outParams.height;
   video_params->codec_id = codec->id;
   video_params->codec_type = AVMEDIA_TYPE_VIDEO;
   video_params->format = _outParams.pfmt;

   _videoCodecContext = ::avcodec_alloc_context3( codec );
   ::avcodec_parameters_to_context( _videoCodecContext, video_st->codecpar );
   _videoCodecContext->time_base.num = 1;
   _videoCodecContext->time_base.den = _outParams.fps;
   _videoCodecContext->gop_size = 40/*12*/; // aka keyframe interval
   _videoCodecContext->max_b_frames = 0;
   if ( _formatContext->oformat->flags & AVFMT_GLOBALHEADER )
      _videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   ::av_opt_set( _videoCodecContext->priv_data, "preset", "fast", 0 );
   ::av_opt_set( _videoCodecContext->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN );

   int status = ::avcodec_open2( _videoCodecContext, nullptr, nullptr );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error opening video codec context" );

   // Ugh... from https://stackoverflow.com/questions/15897849/c-ffmpeg-not-writing-avcc-box-information
   // For the moov:trak:mdia:minf:stbl:stsd:avc1:avcC atom to be created correctly, it's necessary to
   // specify the AV_CODEC_FLAG_GLOBAL_HEADER above to get the video sequence headers into the codec
   // context, and then copy them to the codec parameters before encoding.
   video_st->codecpar->extradata = ( uint8_t * )::av_malloc( _videoCodecContext->extradata_size );
   video_st->codecpar->extradata_size = _videoCodecContext->extradata_size;
   std::memcpy( video_st->codecpar->extradata, _videoCodecContext->extradata, _videoCodecContext->extradata_size );
}

void VideoExporter::initializeAudio( const AVCodec* codec )
{
   AVStream* audio_st = ::avformat_new_stream( _formatContext, nullptr );
   audio_st->time_base.num = 1;
   audio_st->time_base.den = _outParams.audioSampleRate;

   AVCodecParameters* audio_params = audio_st->codecpar;
   audio_params->codec_id = codec->id;
   audio_params->codec_type = AVMEDIA_TYPE_AUDIO;
   audio_params->format = AV_SAMPLE_FMT_FLTP;
   audio_params->channels = 2;
   audio_params->channel_layout = AV_CH_LAYOUT_STEREO;
   audio_params->sample_rate = _outParams.audioSampleRate;
   audio_params->bit_rate = 128000;

   _audioCodecContext = ::avcodec_alloc_context3( codec );
   ::avcodec_parameters_to_context( _audioCodecContext, audio_st->codecpar );
   if ( _formatContext->oformat->flags & AVFMT_GLOBALHEADER )
      _audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   int status = ::avcodec_open2( _audioCodecContext, nullptr, nullptr );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error opening audio codec context" );

   audio_params->frame_size = _audioCodecContext->frame_size;
}

void VideoExporter::initializeFrames()
{
   _colorConversionFrame = ::av_frame_alloc();
   _colorConversionFrame->width = _inParams.width;
   _colorConversionFrame->height = _inParams.height;
   _colorConversionFrame->format = _inParams.pfmt;
   int status = ::av_frame_get_buffer( _colorConversionFrame, 0 );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error initializing color-conversion frame" );

   _videoFrame = ::av_frame_alloc();
   _videoFrame->width = _outParams.width;
   _videoFrame->height = _outParams.height;
   _videoFrame->format = _outParams.pfmt;
   status = ::av_frame_get_buffer( _videoFrame, 0 );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error initializing video frame" );
   _videoFrame->pts = 0LL;

   int flags = SWS_FAST_BILINEAR; // doesn't matter too much since we're just doing a colorspace conversion
   AVPixelFormat inFormat = AV_PIX_FMT_RGB24;
   _swsContext = ::sws_getContext( _inParams.width, _inParams.height, _inParams.pfmt,
                                   _outParams.width, _outParams.height, _outParams.pfmt,
                                   flags, nullptr, nullptr, nullptr );
   if ( _swsContext == nullptr )
      throw std::runtime_error( "VideoExporter - Error initializing color-converter" );

   _audioFrame = ::av_frame_alloc();
   _audioFrame->format = AV_SAMPLE_FMT_FLTP;
   _audioFrame->nb_samples = _audioCodecContext->frame_size;
   _audioFrame->channel_layout = AV_CH_LAYOUT_STEREO;
   _audioFrame->channels = 2;
   _audioFrame->sample_rate = _outParams.audioSampleRate;
   status = ::av_frame_get_buffer( _audioFrame, 0 );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error initializing audio frame" );
   _audioFrame->pts = 0LL;

   auto lambda = [this]() { this->audioFrameFilledCallback(); };
   _audioAccumulator = std::make_unique<AudioAccumulator>( _audioFrame, _audioCodecContext->frame_size, lambda );
}

void VideoExporter::initializePackets()
{
   const int ArbitraryVideoPacketSize = 500000;
   _videoPacket = ::av_packet_alloc();
   ::av_init_packet( _videoPacket );
   int status = ::av_new_packet( _videoPacket, ArbitraryVideoPacketSize );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error initializing video packet" );
   _videoPacket->stream_index = 0;

   const int ArbitraryAudioPacketSize = 200000;
   _audioPacket = ::av_packet_alloc();
   ::av_init_packet( _audioPacket );
   status = ::av_new_packet( _audioPacket, ArbitraryAudioPacketSize );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error initializing audio packet" );
   _audioPacket->stream_index = 1;
}

void VideoExporter::exportEverything()
{
   uint8_t* data[] = { _colorConversionFrame->data[0], nullptr, nullptr, nullptr };
   int stride[] = { _colorConversionFrame->linesize[0], 0, 0, 0 };
   int frameHeight = _colorConversionFrame->height;
   int frameSize = stride[0] * frameHeight;

   int audioFramesPerVideoFrame = _inParams.audioSampleRate / _inParams.fps;
   std::unique_ptr<float[]> leftCh( new float[audioFramesPerVideoFrame] );
   std::unique_ptr<float[]> rightCh( new float[audioFramesPerVideoFrame] );

   for ( uint32_t i = 0; i < _frameCount; )
   {
      _getVideo( data[0], frameSize, i );

      int height = ::sws_scale( _swsContext, data, stride, 0, frameHeight, _videoFrame->data, _videoFrame->linesize );
      if ( height != _videoCodecContext->height )
         throw std::runtime_error( "VideoExporter - color conversion error" );

      int64_t ptsBefore = _videoFrame->pts;
      pushVideo();
      int64_t ptsAfter = _videoFrame->pts;
      int numVideoFramesPushed = int( ( ptsAfter - ptsBefore ) / _ptsIncrement );
      for ( int ii = 0; ii < numVideoFramesPushed; ++ii )
      {
         _getAudio( leftCh.get(), rightCh.get(), audioFramesPerVideoFrame );
         _audioAccumulator->pushAudio( leftCh.get(), rightCh.get(), audioFramesPerVideoFrame );
      }
      // leftovers
      int videoPushedinMS = numVideoFramesPushed * 1000 / _outParams.fps;
      int numAudioFramesToPush = videoPushedinMS * _outParams.audioSampleRate / 1000;
      int64_t audioTimeAfter = _audioFrame->pts;
      if ( audioTimeAfter < numAudioFramesToPush )
      {
         int n = int( numAudioFramesToPush - audioTimeAfter );
         _getAudio( leftCh.get(), rightCh.get(), n );
         _audioAccumulator->pushAudio( leftCh.get(), rightCh.get(), n );
      }
      //pushAudio();
      i += numVideoFramesPushed;
   }

   //pushBufferedData( _videoPacket, _videoCodecContext,_formatContext );
}

void VideoExporter::completeExport()
{
   int status = ::av_write_trailer( _formatContext );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error writing file trailer" );

   status = ::avio_closep( &_formatContext->pb );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error closing output file" );
}

void VideoExporter::cleanup()
{
   if ( _videoPacket != nullptr )
      ::av_packet_free( &_videoPacket );
   if ( _audioPacket != nullptr )
      ::av_packet_free( &_audioPacket );

   if ( _colorConversionFrame != nullptr )
      ::av_frame_free( &_colorConversionFrame );
   if ( _videoFrame != nullptr )
      ::av_frame_free( &_videoFrame );
   if ( _audioFrame != nullptr )
      ::av_frame_free( &_audioFrame );

   if ( _formatContext != nullptr )
   {
      if ( _formatContext->pb != nullptr )
         ::avio_closep( &_formatContext->pb );
      ::avformat_free_context( _formatContext );
      _formatContext = nullptr;
   }

   if ( _audioCodecContext != nullptr )
      ::avcodec_free_context( &_audioCodecContext );

   if ( _videoCodecContext != nullptr )
      ::avcodec_free_context( &_videoCodecContext );

   if ( _swsContext != nullptr )
   {
      ::sws_freeContext( _swsContext );
      _swsContext = nullptr;
   }
}

void VideoExporter::pushVideo()
{
   int status = 0;
   int64_t ptsBefore = _videoFrame->pts;
   static int numCalls = 0;

   do
   {
      status = ::avcodec_send_frame( _videoCodecContext, _videoFrame );
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error sending video frame to compresssor" );
      _videoFrame->pts += _ptsIncrement;

      status = ::avcodec_receive_packet( _videoCodecContext, _videoPacket );
      if ( status == AVERROR( EAGAIN ) )
         continue;
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error receiving compressed video" );
   } while ( status != 0 );

   if ( status == 0 )
   {
      //_formatContext->streams[0]->cur_dts = ptsBefore;
      //_formatContext->streams[0]->cur_dts += 512;
      if ( ptsBefore == 0LL )
         _formatContext->streams[0]->cur_dts = 0LL;
      else
      {
         ++numCalls;
         _formatContext->streams[0]->cur_dts = numCalls;
      }
      status = ::av_interleaved_write_frame( _formatContext, _videoPacket );
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error writing compressed video frame" );
      //_videoFrame->pts += _ptsIncrement;
   }
}

//void VideoExporter::pushAudio()
//{
//   // todo
//   int x = 1;
//}

void VideoExporter::audioFrameFilledCallback()
{
   int status = 0;
   int64_t ptsBefore = _audioFrame->pts;
   static int numCalls = 0;

   do
   {
      status = ::avcodec_send_frame( _audioCodecContext, _audioFrame );
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error sending audio frame to compresssor" );
      _audioFrame->pts += _audioCodecContext->frame_size;

      status = ::avcodec_receive_packet( _audioCodecContext, _audioPacket );
      if ( status == AVERROR( EAGAIN ) )
         return; // need more input... safe to bail at this point
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error receiving compressed audio" );
   } while ( status != 0 );

   if ( status == 0 )
   {
      //_formatContext->streams[1]->cur_dts = ptsBefore;
      //_formatContext->streams[1]->cur_dts += 44100;
      //if ( ptsBefore == 0LL )
      //   _formatContext->streams[1]->cur_dts = 0LL;
      //else
      //   ++_formatContext->streams[1]->cur_dts;
      if ( ptsBefore == 0LL )
         _formatContext->streams[1]->cur_dts = 0LL;
      else
      {
         ++numCalls;
         _formatContext->streams[1]->cur_dts = numCalls;
      }

      status = ::av_interleaved_write_frame( _formatContext, _audioPacket );
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error writing compressed audio frame" );
      //_audioFrame->pts += _audioCodecContext->frame_size;
   }
}


#else
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#include <stdio.h>

namespace
{
   void my_av_log_callback( void *ptr, int level, const char* fmt, va_list vargs )
   {
      if ( level <= 24 )
      {
         char message[2048];
         ::vsnprintf( message, 2048, fmt, vargs );
         int x = 1;
      }
   }

   void pushSomeAudio( AVFrame* frame, AVPacket* pkt, AVCodecContext* cc, AVFormatContext* fc )
   {
      int status = 0;

      do
      {
         frame->nb_samples = cc->frame_size;

         //todo - get some actual audio!!
         //std::memcpy( frame->buf[0]->data, leftCh, samplesToPush * 4 ); leftCh += samplesToPush;
         //std::memcpy( frame->buf[1]->data, rightCh, samplesToPush * 4 ); rightCh += samplesToPush;

         status = ::avcodec_send_frame( cc, frame );
         if ( status < 0 )
         {
            int x = 1;
         }
         frame->pts += frame->nb_samples;
         frame->pkt_dts += frame->nb_samples;

         status = ::avcodec_receive_packet( cc, pkt );
         if ( status == AVERROR( EAGAIN ) )
            continue;
         if ( status < 0 )
         {
            int x = 1;
         }
      } while ( status != 0 );

#if 0
      if (/* pkt->size > 0*/status == 0 )
      {
         status = ::av_interleaved_write_frame( fc, pkt );
         if ( status < 0 )
         {
            int x = 1;
         }
         printf( "  wrote audio to file\n" );
      }
#endif
   }

   void pushSomeVideo( AVFrame* frame, AVPacket* pkt, AVCodecContext* cc, AVFormatContext* fc, int64_t ptsIncrement )
   {
      int status = 0;

      do
      {
         //todo - get some actual video!!

         status = ::avcodec_send_frame( cc, frame );
         if ( status < 0 )
         {
            int x = 1;
         }
         frame->pts += ptsIncrement;
         frame->pkt_dts += ptsIncrement;

         status = ::avcodec_receive_packet( cc, pkt );
         if ( status == AVERROR( EAGAIN ) )
            continue;
         if ( status < 0 )
         {
            int x = 1;
         }
      } while ( status != 0 );

#if 0
      if (/*pkt->size > 0*/status == 0 )
      {
         status = ::av_interleaved_write_frame( fc, pkt );
         if ( status < 0 )
         {
            int x = 1;
         }
         printf( "  wrote video to file\n" );
      }
#endif
   }

   void pushBufferedData( AVPacket* pkt, AVCodecContext* cc, AVFormatContext* fc )
   {
      int status = ::avcodec_send_frame( cc, nullptr );
      while ( 1 )
      {
         status = ::avcodec_receive_packet( cc, pkt );
         if ( status == 0 )
         {
            status = ::av_interleaved_write_frame( fc, pkt );
         }
         else if ( status == AVERROR_EOF )
            break;
      }
   }
}

VideoExporter::VideoExporter( const std::string& outPath, const Params& inParams, const Params& outParams )
   : _path( outPath )
   , _inParams( inParams )
   , _outParams( outParams )
{
   const int Width = 128;
   const int Height = 96;
   const int FrameRate = 20;
   const int BitDepth = 24;

   //::av_log_set_callback( my_av_log_callback );

   AVOutputFormat* fmt = ::av_guess_format( nullptr, outPath.c_str(), nullptr );
   const AVCodec* videoCodec = ::avcodec_find_encoder( fmt->video_codec );
   const AVCodec* audioCodec = ::avcodec_find_encoder( fmt->audio_codec );

   AVFormatContext *formatContext = nullptr;
   int status = ::avformat_alloc_output_context2( &formatContext, fmt, nullptr, outPath.c_str() );

   //
   // video init
   //
   AVStream* video_st = ::avformat_new_stream( formatContext, nullptr );
   video_st->time_base.num = 1;
   video_st->time_base.den = FrameRate;

   AVCodecParameters* video_params = video_st->codecpar;
   video_params->width = Width;
   video_params->height = Height;
   video_params->codec_id = fmt->video_codec;
   video_params->codec_type = AVMEDIA_TYPE_VIDEO;
   video_params->format = AV_PIX_FMT_YUV420P;

   AVCodecContext* videoCodecContext = ::avcodec_alloc_context3( videoCodec );
   status = ::avcodec_parameters_to_context( videoCodecContext, video_st->codecpar );
   videoCodecContext->time_base.num = 1;
   videoCodecContext->time_base.den = FrameRate;
   videoCodecContext->gop_size = 40/*12*/; // aka keyframe interval
   videoCodecContext->max_b_frames = 0;
   if ( formatContext->oformat->flags & AVFMT_GLOBALHEADER )
      videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   ::av_opt_set( videoCodecContext->priv_data, "preset", "fast", 0 );
   ::av_opt_set( videoCodecContext->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN );

   status = ::avcodec_open2( videoCodecContext, nullptr, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }

   auto tb = videoCodecContext->time_base;
   int x = 1;

   // Ugh... from https://stackoverflow.com/questions/15897849/c-ffmpeg-not-writing-avcc-box-information
   // For the moov:trak:mdia:minf:stbl:stsd:avc1:avcC atom to be created correctly, it's necessary to
   // specify the AV_CODEC_FLAG_GLOBAL_HEADER above to get the video sequence headers into the codec
   // context, and then copy them to the codec parameters before encoding.
   video_st->codecpar->extradata = (uint8_t *)::av_malloc( videoCodecContext->extradata_size );
   video_st->codecpar->extradata_size = videoCodecContext->extradata_size;
   std::memcpy( video_st->codecpar->extradata, videoCodecContext->extradata, videoCodecContext->extradata_size );

   //
   // audio init
   //
   AVStream* audio_st = ::avformat_new_stream( formatContext, nullptr );
   audio_st->time_base.num = 1;
   audio_st->time_base.den = 44100;

   AVCodecParameters* audio_params = audio_st->codecpar;
   audio_params->codec_id = fmt->audio_codec;
   audio_params->codec_type = AVMEDIA_TYPE_AUDIO;
   audio_params->format = AV_SAMPLE_FMT_FLTP;
   audio_params->channels = 2;
   audio_params->channel_layout = AV_CH_LAYOUT_STEREO;
   audio_params->sample_rate = 44100;
   audio_params->bit_rate = 128000;

   AVCodecContext* audioCodecContext = ::avcodec_alloc_context3( audioCodec );
   status = ::avcodec_parameters_to_context( audioCodecContext, audio_st->codecpar );
   if ( formatContext->oformat->flags & AVFMT_GLOBALHEADER )
      audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   status = ::avcodec_open2( audioCodecContext, nullptr, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }
   audio_params->frame_size = audioCodecContext->frame_size;

   //
   // gotta see if we can open file for output...
   //
   status = ::avio_open( &formatContext->pb, outPath.c_str(), AVIO_FLAG_WRITE );

   //
   // prepare to write... don't trust ::avformat_init_output() telling you that
   // a call to ::avformat_write_header() is unnecessary. If you don't call it,
   // the stream(s) won't be packaged in an MP4 container. Also, the stream's
   // time_base appears to be updated within this call.
   //
   status = ::avformat_write_header( formatContext, nullptr );

   int64_t ptsIncrement = video_st->time_base.den / FrameRate;

   AVFrame *frame = ::av_frame_alloc();
   frame->format = videoCodecContext->pix_fmt;
   frame->width = videoCodecContext->width;
   frame->height = videoCodecContext->height;
   status = ::av_frame_get_buffer( frame, 0 );

   int sws_flags = SWS_FAST_BILINEAR; // doesn't matter too much since we're just doing a colorspace conversion
   AVPixelFormat inFormat = AV_PIX_FMT_RGB24;
   SwsContext* sws_ctx = ::sws_getContext( Width, Height, inFormat,
                                           videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,
                                           sws_flags, nullptr, nullptr, nullptr );

   // Init video to all-red
   AVFrame* srcImage = ::av_frame_alloc();
   srcImage->width = Width;
   srcImage->height = Height;
   srcImage->format = inFormat;
   status = ::av_frame_get_buffer( srcImage, 0 );
   {
      const int BufSize = Width * Height * 3;
      uint8_t* buf = srcImage->data[0];
      for ( int i = 0; i < BufSize; ++i )
         buf[i] = ( i % 3 == 0 ) ? 0xff : 0x00;
   }

   const int ArbitraryVideoPacketSize = 500000;
   AVPacket *videoPacket = ::av_packet_alloc();
   ::av_init_packet( videoPacket );
   status = ::av_new_packet( videoPacket, ArbitraryVideoPacketSize );
   videoPacket->stream_index = 0;

   const int ArbitraryAudioPacketSize = 200000;
   AVPacket *audioPacket = ::av_packet_alloc();
   ::av_init_packet( audioPacket );
   status = ::av_new_packet( audioPacket, ArbitraryAudioPacketSize );
   audioPacket->stream_index = 1;

   // Init audio to all-silence
   AVFrame *srcAudio = ::av_frame_alloc();
   srcAudio->format = AV_SAMPLE_FMT_FLTP;
   srcAudio->nb_samples = audioCodecContext->frame_size;
   srcAudio->channel_layout = AV_CH_LAYOUT_STEREO;
   srcAudio->channels = 2;
   srcAudio->sample_rate = 44100;
   status = ::av_frame_get_buffer( srcAudio, 0 );

   std::memset( srcAudio->data[0], 0, srcAudio->nb_samples * 4 );
   std::memset( srcAudio->data[1], 0, srcAudio->nb_samples * 4 );

   //
   // do the colorspace conversion
   //
   uint8_t* data[] = { srcImage->data[0], nullptr, nullptr, nullptr };
   int stride[] = { srcImage->linesize[0], 0, 0, 0 };
   status = ::sws_scale( sws_ctx, data, stride, 0, Height, frame->data, frame->linesize );
   if ( status != videoCodecContext->height )
   {
      int x = 1;
   }

   //
   // the big loop
   //
   const double Duration = 30.; /*seconds*/
   frame->pts = srcAudio->pts = 0LL;
   frame->pkt_dts = srcAudio->pkt_dts = 0LL;
   const double AudioTimeBase = ::av_q2d( audio_st->time_base );
   const double VideoTimeBase = ::av_q2d( video_st->time_base );
   while ( 1 )
   {
#if 0
      int status;
      double audio_time_start = srcAudio->pts * AudioTimeBase;
      double video_time_start = frame->pts * VideoTimeBase;

      pushSomeAudio( srcAudio, audioPacket, audioCodecContext, formatContext );
      pushSomeVideo( frame, videoPacket, videoCodecContext, formatContext, ptsIncrement );

      double audio_time_end = srcAudio->pts * AudioTimeBase;
      double video_time_end = frame->pts * VideoTimeBase;

      // We'll get a huge chunk of video (1.75 seconds, so push that first...
      status = ::av_interleaved_write_frame( formatContext, videoPacket );

      while ( audio_time_end < video_time_end ) // we'll get just a tiny chunk of audio
      {
         status = ::av_interleaved_write_frame( formatContext, audioPacket );
         pushSomeAudio( srcAudio, audioPacket, audioCodecContext, formatContext );
         audio_time_end = srcAudio->pts * AudioTimeBase;
      }

      break;

#else
      double audio_time = srcAudio->pts * AudioTimeBase;
      double video_time = frame->pts * VideoTimeBase;
      if ( audio_time >= Duration || video_time >= Duration )
         break;

      if ( audio_time < video_time )
      {
         printf( "Writing audio starting at %lf\n", audio_time );
         pushSomeAudio( srcAudio, audioPacket, audioCodecContext, formatContext );
         status = ::av_interleaved_write_frame( formatContext, audioPacket );
         break;
      }
      else
      {
         printf( "Writing video starting at %lf\n", video_time );
         pushSomeVideo( frame, videoPacket, videoCodecContext, formatContext, ptsIncrement );
         status = ::av_interleaved_write_frame( formatContext, videoPacket );
      }
#endif
   }

   //
   // Write out any buffered video & audio
   //
#if 1
   pushBufferedData( videoPacket, videoCodecContext, formatContext );
   pushBufferedData( audioPacket, audioCodecContext, formatContext );
#endif

   //
   // Finalize the file output and cleanup
   //
   status = ::av_write_trailer( formatContext );
   status = ::avio_closep( &formatContext->pb );

   ::av_frame_free( &frame );
   ::av_frame_free( &srcImage );
   ::av_packet_free( &videoPacket );

   ::sws_freeContext( sws_ctx );
   ::avcodec_free_context( &videoCodecContext );
   ::avcodec_free_context( &audioCodecContext );
   ::avformat_free_context( formatContext );
}

VideoExporter::~VideoExporter()
{
   ::av_log_set_callback( nullptr );
}

bool VideoExporter::initialize()
{
   return false;
}

bool VideoExporter::deliverFrame( const uint8_t* frame, int frameSize )
{
   return false;
}

bool VideoExporter::completeExport()
{
   return false;
}
#endif