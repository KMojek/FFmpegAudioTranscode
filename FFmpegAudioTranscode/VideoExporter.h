#pragma once

extern "C"
{
   struct AVCodec;
   struct AVCodecContext;
   struct AVFormatContext;
   struct AVFrame;
   struct AVPacket;
   struct SwsContext;
}

#include <cstdint>
#include <functional>
#include <string>

class VideoExporter
{
public:
   struct Params
   {
      int   pfmt;             // AVPixelFormat enum
      int   width;
      int   height;
      int   fps;              // limited to constant-FPS input and output currently
      int   audioSampleRate;  // assumes stereo input/output
   };

   // Callbacks provide the video and audio for each frame
   typedef std::function< bool( uint8_t* /*buf*/, int/*bufSize*/, unsigned /*frameIndex*/ ) > GetVideoFrameCb;
   typedef std::function< bool( float* /*leftCh*/, float* /*rightCh*/, int /*frameSize*/ ) > GetAudioFrameCb;

   // Callback to allow the exporter to query the client on whether to abort the export
   typedef std::function< bool() > QueryForCancelCb;

   // Callback to allow the exporter to report export progress to the client (0-100 scale)
   typedef std::function< void( int ) > ProgressReportCb;

   VideoExporter( const std::string& outPath, const Params& inParams );
   virtual ~VideoExporter();

   void setGetVideoCallback( GetVideoFrameCb fn ) { _getVideo = fn; }
   void setGetAudioCallback( GetAudioFrameCb fn ) { _getAudio = fn; }
   void setQueryForCancelCallback( QueryForCancelCb fn ) { _queryForCancel = fn; }
   void setProgressReportCallback( ProgressReportCb fn ) { _progressReporter = fn; }

   void initialize();
   void exportVideoAndAudio( int videoFrameCount );
   void completeExport();

protected:
   void initializeVideo( const AVCodec* codec );
   void initializeAudio( const AVCodec* codec );
   void initializeFrames();
   void initializePackets();

   int pushVideoUntilPacketFilled( int startFrameIndex );
   void pushAudioUntilPacketFilled();

   void cleanup();

   const std::string       _path;
   const Params            _inParams;
   Params                  _outParams;
   int64_t                 _ptsIncrement = 0LL;
   SwsContext*             _swsContext = nullptr;
   AVFormatContext*        _formatContext = nullptr;
   AVCodecContext*         _videoCodecContext = nullptr;
   AVCodecContext*         _audioCodecContext = nullptr;
   AVFrame*                _colorConversionFrame = nullptr;
   AVFrame*                _videoFrame = nullptr;
   AVFrame*                _audioFrame = nullptr;
   AVPacket*               _videoPacket = nullptr;
   AVPacket*               _audioPacket = nullptr;
   GetVideoFrameCb         _getVideo = nullptr;
   GetAudioFrameCb         _getAudio = nullptr;
   QueryForCancelCb        _queryForCancel = nullptr;
   ProgressReportCb        _progressReporter = nullptr;
};