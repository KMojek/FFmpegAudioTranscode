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
      for ( int i = 0; i < frameSize; ++i )
      {
         leftCh[i] = 0.6f;
         rightCh[i] = -0.4f;
      }

      return true;
   }

   void pushBufferedData( AVPacket* pkt, AVCodecContext* cc, AVFormatContext* fc, int streamIndex )
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

   void update( const float **l, const float** r, int *count, int n )
   {
      *l += n;
      *r += n;
      *count -= n;
   }

   void my_av_log_callback( void* ptr, int level, const char*fmt, va_list vargs )
   {
      char message[2048];

      if ( level <= 16 )
      {
         ::vsnprintf( message, 2048, fmt, vargs );
         int x = 1;
      }
   }

   int16_t convertAudioSample( float val )
   {
      return int16_t( std::floorf( val * 32767 ) );
   }
}

VideoExporter::AudioAccumulator::AudioAccumulator( AVFrame* frame, int frameSize, std::function< void() > frameReadyCallback )
   : _frame( frame )
   , _frameSize( frameSize )
   , _frameReadyCallback( frameReadyCallback )
{
   if ( _frame->buf[0]->data == nullptr || _frame->buf[1]->data == nullptr )
      throw std::runtime_error( "VideoExporter audio accumulator - invalid audio frame" );

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

   //::av_log_set_callback( my_av_log_callback );
}

VideoExporter::~VideoExporter()
{
   cleanup();

   ::av_log_set_callback( nullptr );
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
   video_st->id = _formatContext->nb_streams - 1;

   _videoCodecContext = ::avcodec_alloc_context3( codec );
   _videoCodecContext->time_base.num = 1;
   _videoCodecContext->time_base.den = _outParams.fps;
   _videoCodecContext->gop_size = 40/*12*/; // aka keyframe interval
   _videoCodecContext->max_b_frames = 0;
   _videoCodecContext->width = _outParams.width;
   _videoCodecContext->height = _outParams.height;
   _videoCodecContext->pix_fmt = _outParams.pfmt;
   if ( _formatContext->oformat->flags & AVFMT_GLOBALHEADER )
      _videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   ::av_opt_set( _videoCodecContext->priv_data, "preset", "fast", 0 );
   ::av_opt_set( _videoCodecContext->priv_data, "crf", "18", AV_OPT_SEARCH_CHILDREN );

   int status = ::avcodec_open2( _videoCodecContext, nullptr, nullptr );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error opening video codec context" );

   status = ::avcodec_parameters_from_context( video_st->codecpar, _videoCodecContext );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error setting video stream parameters" );
}

void VideoExporter::initializeAudio( const AVCodec* codec )
{
   AVStream* audio_st = ::avformat_new_stream( _formatContext, nullptr );
   audio_st->time_base.num = 1;
   audio_st->time_base.den = _outParams.audioSampleRate;
   audio_st->id = _formatContext->nb_streams - 1;

   _audioCodecContext = ::avcodec_alloc_context3( codec );
   _audioCodecContext->channels = 2;
   _audioCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
   _audioCodecContext->sample_rate = _outParams.audioSampleRate;
   _audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
   _audioCodecContext->bit_rate = 128000;

   if ( _formatContext->oformat->flags & AVFMT_GLOBALHEADER )
      _audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

   int status = ::avcodec_open2( _audioCodecContext, nullptr, nullptr );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error opening audio codec context" );

   status = ::avcodec_parameters_from_context( audio_st->codecpar, _audioCodecContext );
   if ( status != 0 )
      throw std::runtime_error( "VideoExporter - Error setting audio stream parameters" );
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

   if ( _audioCodecContext != nullptr )
   {
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

   // I think what we need here is to accumulate enough compressed data for however
   // many streams we have in order to do a first av_interleaved_write_frame() call
   // on each stream. For video, that's going to be a full 35 frames. Not sure for
   // audio but seems like we need that to 

   // should we start out with some audio in order to avoid negative timestamps?
   //_getAudio( leftCh.get(), rightCh.get(), audioFramesPerVideoFrame );
   //_audioAccumulator->pushAudio( leftCh.get(), rightCh.get(), audioFramesPerVideoFrame );

   // Argh... current-status: if we stick to video-only, we get exactly the file we expect.
   // Limiting here to first 35 video frames but seems true for any length. As soon as we
   // do anything with audio, things start to get weird.
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
#if 1
      for ( int ii = 0; ii < numVideoFramesPushed; ++ii )
      {
         _getAudio( leftCh.get(), rightCh.get(), audioFramesPerVideoFrame );
         _audioAccumulator->pushAudio( leftCh.get(), rightCh.get(), audioFramesPerVideoFrame );
      }
#endif
#if 0
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
#endif
      i += numVideoFramesPushed;
   }

#if 0
   if ( _audioFrame->nb_samples > 0 )
   {
      int status = ::avcodec_send_frame( _audioCodecContext, _audioFrame );
      //_audioFrame->pts += _audioCodecContext->frame_size;

      status = ::avcodec_receive_packet( _audioCodecContext, _audioPacket );

      ++_formatContext->streams[1]->cur_dts;

      status = ::av_interleaved_write_frame( _formatContext, _audioPacket );
   }
#endif

   if ( _audioCodecContext != nullptr )
      pushBufferedData( _audioPacket, _audioCodecContext, _formatContext, 1 );
   pushBufferedData( _videoPacket, _videoCodecContext, _formatContext, 0 );

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
      _videoPacket->stream_index = 0;
      status = ::av_interleaved_write_frame( _formatContext, _videoPacket );
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error writing compressed video frame" );
   }
}

void VideoExporter::audioFrameFilledCallback()
{
   int status = 0;

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
      _audioPacket->stream_index = 1;
      status = ::av_interleaved_write_frame( _formatContext, _audioPacket );
      if ( status < 0 )
         throw std::runtime_error( "VideoExporter - error writing compressed audio frame" );
   }
}
