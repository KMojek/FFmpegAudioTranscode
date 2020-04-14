#include "stdafx.h"

#include "VideoExporter.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

//#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
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

   AVOutputFormat* fmt = ::av_guess_format( "avi", nullptr, nullptr );

   // it's gonna guess MP4 AV_CODEC_ID_MPEG4 for video but we want uncompressed...
   fmt->video_codec = AV_CODEC_ID_RAWVIDEO;

   AVFormatContext *formatContext = nullptr;
   int status = ::avformat_alloc_output_context2( &formatContext, fmt, nullptr, outPath.c_str() );
 
   const AVCodec *codec = ::avcodec_find_encoder( fmt->video_codec );

   AVStream* video_st = ::avformat_new_stream( formatContext, nullptr );
   video_st->time_base.num = 1;
   video_st->time_base.den = FrameRate;

   video_st->codecpar->width = Width;
   video_st->codecpar->height = Height;
   video_st->codecpar->codec_id = fmt->video_codec;
   video_st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
   video_st->codecpar->format = AV_PIX_FMT_NONE;
   video_st->codecpar->bits_per_coded_sample = BitDepth;

   AVCodecContext* codecContext = ::avcodec_alloc_context3( codec );

   status = ::avcodec_parameters_to_context( codecContext, video_st->codecpar );
   codecContext->bit_rate = 400000;
   codecContext->width = Width;
   codecContext->height = Height;
   codecContext->time_base.num = 1;
   codecContext->time_base.den = FrameRate;
   codecContext->pix_fmt = AV_PIX_FMT_BGR24;

   status = ::avcodec_open2( codecContext, nullptr, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }

   status = ::avformat_init_output( formatContext, nullptr );
   if ( status != 0 )
   {
      int x = 1;
   }

   AVFrame *frame = ::av_frame_alloc();
   frame->format = codecContext->pix_fmt;
   frame->width = codecContext->width;
   frame->height = codecContext->height;
   status = ::av_image_alloc( frame->data, frame->linesize, frame->width, frame->height, AVPixelFormat(frame->format), 1 );

#if 0
   int sws_flags = SWS_BICUBIC;
   AVPixelFormat inFormat = AV_PIX_FMT_RGB24;
   SwsContext* sws_ctx = ::sws_getContext( 128, 96, inFormat,
                                           codecContext->width, codecContext->height, codecContext->pix_fmt,
                                           sws_flags, nullptr, nullptr, nullptr );

   AVFrame* srcImage = ::av_frame_alloc();
   status = ::av_image_alloc( srcImage->data, srcImage->linesize, 128, 96, inFormat, 1 );
#endif

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
      status = ::avcodec_send_frame( codecContext, frame );
      if ( status != 0 )
      {
         int x = 1;
      }

      if ( status == 0 )
      {
         while ( 1 )
         {
            status = ::avcodec_receive_packet( codecContext, pkt );
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
      status = ::avcodec_send_frame( codecContext, nullptr );
      while ( 1 )
      {
         status = ::avcodec_receive_packet( codecContext, pkt );
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
   ::avcodec_free_context( &codecContext );
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