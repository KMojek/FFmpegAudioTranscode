#include "stdafx.h"

#include "VideoExporter.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstdint>
#include <memory>

#include <memory.h>

namespace
{

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

   video_st->codecpar->width = Width;
   video_st->codecpar->height = Height;
   video_st->codecpar->codec_id = fmt->video_codec;
   video_st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
   video_st->codecpar->format = AV_PIX_FMT_YUV420P;

   AVCodecContext* videoCodecContext = ::avcodec_alloc_context3( videoCodec );
   status = ::avcodec_parameters_to_context( videoCodecContext, video_st->codecpar );
   videoCodecContext->bit_rate = 400000;
   videoCodecContext->width = Width;
   videoCodecContext->height = Height;
   videoCodecContext->time_base.num = 1;
   videoCodecContext->time_base.den = FrameRate;
   //videoCodecContext->framerate.num = FrameRate;
   //videoCodecContext->framerate.den = 1;
   videoCodecContext->gop_size = /*20*/12;
   videoCodecContext->max_b_frames = 0;

   ::av_opt_set( videoCodecContext->priv_data, "preset", "fast", 0 );
   ::av_opt_set( videoCodecContext->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN );
   //::av_opt_set( videoCodecContext->priv_data, "qp", "0", 0 ); // ???

   status = ::avcodec_open2( videoCodecContext, nullptr, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }

   //
   // audio init
   //
#if 0
   AVStream* audio_st = ::avformat_new_stream( formatContext, nullptr );
   audio_st->time_base.num = 1;
   audio_st->time_base.den = 44100;

   audio_st->codecpar->codec_id = fmt->audio_codec;
   audio_st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
   audio_st->codecpar->format = AV_SAMPLE_FMT_FLTP;
   audio_st->codecpar->channels = 2;
   audio_st->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
   audio_st->codecpar->sample_rate = 44100;
   audio_st->codecpar->frame_size = 44100 / FrameRate;

   AVCodecContext* audioCodecContext = ::avcodec_alloc_context3( audioCodec );
   status = ::avcodec_parameters_to_context( audioCodecContext, audio_st->codecpar );

   status = ::avcodec_open2( audioCodecContext, nullptr, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }
#endif

   //
   // gotta see if we can open file for output...
   //
   status = ::avio_open( &formatContext->pb, outPath.c_str(), AVIO_FLAG_WRITE );

   //
   // prepare to write...
   //
   //int initStatus = ::avformat_init_output( formatContext, nullptr );
   //if ( initStatus == AVSTREAM_INIT_IN_WRITE_HEADER )
   //{
   //   int x = 1;
   //   // todo - avformat_write_header()
   //}
   //else if ( initStatus == AVSTREAM_INIT_IN_INIT_OUTPUT )
   //{
      status = ::avformat_write_header( formatContext, nullptr );
   //   int x = 1;
   //}
   //else if ( initStatus < 0 )
   //{
   //   int x = 1;
   //}

   AVFrame *frame = ::av_frame_alloc();
   frame->format = videoCodecContext->pix_fmt;
   frame->width = videoCodecContext->width;
   frame->height = videoCodecContext->height;
   //status = ::av_image_alloc( frame->data, frame->linesize, frame->width, frame->height, AVPixelFormat( frame->format ), 1 );
   status = ::av_frame_get_buffer( frame, 1 );

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
   //status = ::av_image_alloc( srcImage->data, srcImage->linesize, Width, Height, inFormat, 1 );
   status = ::av_frame_get_buffer( srcImage, 1 );
   {
      const int BufSize = Width * Height * 3;
      uint8_t* buf = srcImage->data[0];
      for ( int i = 0; i < BufSize; ++i )
         buf[i] = ( i % 3 == 0 ) ? 0xff : 0x00;
   }

   // sanity checks... we should be able to write to either AVFrame
   bool foo = ::av_buffer_is_writable( frame->buf[0] );
   bool bar = ::av_buffer_is_writable( srcImage->buf[0] );

   AVPacket *videoPacket = ::av_packet_alloc();
   status = ::av_new_packet( videoPacket, 400000 );
   // sanity check... packet should also be writable
   bool bletch = ::av_buffer_is_writable( videoPacket->buf );


   // Init audio to all-silence
   AVFrame* srcAudio = ::av_frame_alloc();
   int size = 44100 / FrameRate * 2 /*channels*/ * 4 /*sizeof(float)*/;
   srcAudio->data[0] = (uint8_t *)::av_malloc( size );
   srcAudio->linesize[0] = size; // not sure how stride works for audio??
   srcAudio->nb_samples = 44100;
   ::memset( srcAudio->data[0], 0, size );

   //
   // We're ready to loop over the frames...
   //

   const int MaxFrames = 200;
   frame->pts = 0LL;
   for ( int i = 0; i < /*20*/1; ++i )
   {
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

      int numFrames = 0;
      while ( numFrames <= MaxFrames )
      {
         int numSendFrames = 1;
         while ( 1 )
         {
            status = ::avcodec_send_frame( videoCodecContext, frame );
            if ( status < 0 )
            {
               int x = 1;
            }
            frame->pts += 1LL;

            status = ::avcodec_receive_packet( videoCodecContext, videoPacket );
            if ( status == AVERROR( EAGAIN ) )
            {
               ++numSendFrames;
               continue;
            }
            if ( status == 0 )
               break;
         }

         if ( status == 0 )
         {
            status = ::av_interleaved_write_frame( formatContext, videoPacket );
            numFrames += numSendFrames;
         }
      }

      // write out any buffered video...
      status = ::avcodec_send_frame( videoCodecContext, nullptr );
      //frame->pts += 1LL;
      int numPacketsReceivedAfterFlush = 0;
      while ( 1 )
      {
         status = ::avcodec_receive_packet( videoCodecContext, videoPacket );
         if ( videoPacket->size == 0 )
            break;
         if ( status == 0 )
         {
            status = ::av_interleaved_write_frame( formatContext, videoPacket );
         //   frame->pts += 1LL;
            ++numFrames;
         }
         else if ( status == AVERROR_EOF )
            break;
         ++numPacketsReceivedAfterFlush;
      }
      //status = ::av_interleaved_write_frame( formatContext, videoPacket );
      //if ( status == 0 )
      //   numFrames += 1;
      int x = 1;


      //
      // todo - push one "video frame worth of audio"
      //


   }

   //
   // buffered video
   //
   int64_t frameCount = frame->pts;

   status = ::av_write_trailer( formatContext );

   status = ::avio_close( formatContext->pb );

   ::sws_freeContext( sws_ctx );
   ::avcodec_free_context( &videoCodecContext );
   //::avcodec_free_context( &audioCodecContext );
   ::avformat_free_context( formatContext );
}


#if 0
   status = ::avio_open( &formatContext->pb, outPath.c_str(), AVIO_FLAG_WRITE );

   status = ::avformat_write_header( formatContext, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }

   std::unique_ptr<uint8_t[]> buf;
   const int BufSize = 128 * 96 * 3;
   buf.reset( new uint8_t[BufSize] );

   // Init to all-red
   for ( int i = 0; i < BufSize; ++i )
      buf[i] = ( i % 3 == 2 ) ? 0xff : 0x00;

   frame->pts = 0LL;

   // Render 100 frames
   for ( int i = 0; i < 100; ++i )
   {
      ::memcpy( frame->data[0], buf.get(), BufSize );

      AVPacket *pkt = ::av_packet_alloc();
      ::av_init_packet( pkt );
      status = ::avcodec_send_frame( videoCodecContext, frame );
      if ( status != 0 )
      {
         int x = 1;
      }

      if ( status == 0 )
      {
         while ( 1 )
         {
            status = ::avcodec_receive_packet( videoCodecContext, pkt );
            if ( status == 0 )
            {
               pkt->duration = 1;
               status = ::av_interleaved_write_frame( formatContext, pkt );
               if ( status != 0 )
               {
                  int x = 1;
               }

            }
            else if ( status == AVERROR(EAGAIN) )
               break;
         }
      }

      frame->pts += 1;

      ::av_packet_free( &pkt );
   }

   // Render any buffered data
   {
      AVPacket *pkt = ::av_packet_alloc();
      ::av_init_packet( pkt );
      status = ::avcodec_send_frame( videoCodecContext, nullptr );
      while ( 1 )
      {
         status = ::avcodec_receive_packet( videoCodecContext, pkt );
         if ( status == 0 )
            status = ::av_interleaved_write_frame( formatContext, pkt );
         else if ( status == AVERROR_EOF )
            break;
      }

      ::av_packet_free( &pkt );
   }

   status = ::av_write_trailer( formatContext );
   status = ::avio_close( formatContext->pb );

   //::sws_freeContext( sws_ctx );
   ::avcodec_free_context( &videoCodecContext );
   ::avformat_free_context( formatContext );
#endif

VideoExporter::~VideoExporter()
{

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