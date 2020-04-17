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
   videoCodecContext->width = Width;
   videoCodecContext->height = Height;
   videoCodecContext->time_base.num = 1;
   videoCodecContext->time_base.den = FrameRate;
   videoCodecContext->gop_size = 40/*12*/; // aka keyframe interval
   videoCodecContext->max_b_frames = 0;
   videoCodecContext->flags = AV_CODEC_FLAG_GLOBAL_HEADER;

   ::av_opt_set( videoCodecContext->priv_data, "preset", "fast", 0 );
   ::av_opt_set( videoCodecContext->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN );

   status = ::avcodec_open2( videoCodecContext, nullptr, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }

   // Ugh... from https://stackoverflow.com/questions/15897849/c-ffmpeg-not-writing-avcc-box-information
   // For the moov:trak:mdia:minf:stbl:stsd:avc1:avcC atom to be created correctly, it's necessary to
   // specify the AV_CODEC_FLAG_GLOBAL_HEADER above to get the video sequence headers into the codec
   // context, and then copy them to the codec parameters before encoding.
   video_st->codecpar->extradata = (uint8_t *)::av_malloc( videoCodecContext->extradata_size );
   video_st->codecpar->extradata_size = videoCodecContext->extradata_size;
   ::memcpy( video_st->codecpar->extradata, videoCodecContext->extradata, videoCodecContext->extradata_size );

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
   status = ::av_frame_get_buffer( srcImage, 1 );
   {
      const int BufSize = Width * Height * 3;
      uint8_t* buf = srcImage->data[0];
      for ( int i = 0; i < BufSize; ++i )
         buf[i] = ( i % 3 == 0 ) ? 0xff : 0x00;
   }

   const int ArbitraryPacketSize = 500000;
   AVPacket *videoPacket = ::av_packet_alloc();
   ::av_init_packet( videoPacket );
   status = ::av_new_packet( videoPacket, ArbitraryPacketSize );

   // Init audio to all-silence
   //AVFrame* srcAudio = ::av_frame_alloc();
   //int size = 44100 / FrameRate * 2 /*channels*/ * 4 /*sizeof(float)*/;
   //srcAudio->data[0] = (uint8_t *)::av_malloc( size );
   //srcAudio->linesize[0] = size; // not sure how stride works for audio??
   //srcAudio->nb_samples = 44100;
   //::memset( srcAudio->data[0], 0, size );

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
   const int MaxFrames = 1000;
   frame->pts = 0LL;
   int numFrames = 0;
   while ( numFrames <= MaxFrames )
   {
      while ( 1 )
      {
         status = ::avcodec_send_frame( videoCodecContext, frame );
         if ( status < 0 )
         {
            int x = 1;
         }
         frame->pts += ptsIncrement;

         status = ::avcodec_receive_packet( videoCodecContext, videoPacket );
         if ( status == AVERROR( EAGAIN ) )
            continue;
         if ( status == 0 )
            break;
      }

      if ( status == 0 )
      {
         status = ::av_interleaved_write_frame( formatContext, videoPacket );
         // todo - push one "video frame worth of audio"
         if ( status == 0 )
            ++numFrames;
      }
   }

   // write out any buffered video...
   status = ::avcodec_send_frame( videoCodecContext, nullptr );
   frame->pts += ptsIncrement;
   int numPacketsReceivedAfterFlush = 0;
   while ( 1 )
   {
      status = ::avcodec_receive_packet( videoCodecContext, videoPacket );
      if ( status == 0 )
      {
         //videoPacket->duration = 1LL;
         status = ::av_interleaved_write_frame( formatContext, videoPacket );
         if ( status == 0 )
            ++numFrames;
      }
      else if ( status == AVERROR_EOF )
         break;
      ++numPacketsReceivedAfterFlush;
   }

   // todo - write out any buffered audio


   //
   // Finalize the file output and cleanup
   //
   status = ::av_write_trailer( formatContext );
   status = ::avio_close( formatContext->pb );

   ::av_frame_free( &frame );
   ::av_frame_free( &srcImage );
   ::av_packet_free( &videoPacket );

   ::sws_freeContext( sws_ctx );
   ::avcodec_free_context( &videoCodecContext );
   //::avcodec_free_context( &audioCodecContext );
   ::avformat_free_context( formatContext );
}

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