#pragma once

extern "C"
{
   enum AVPixelFormat;

   struct AVCodec;
   struct AVCodecContext;
   struct AVFormatContext;
   struct AVFrame;
   struct AVPacket;
   struct SwsContext;
}

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class VideoExporter
{
public:
   struct Params
   {
      AVPixelFormat  pfmt;
      int            width;
      int            height;
      int            fps;     // limited to constant-FPS input and output currently
      int            audioSampleRate; // assumes stereo input/output
   };

   // Callbacks provide the video and audio for each frame
   typedef std::function< bool( uint8_t* /*buf*/, int/*bufSize*/, unsigned /*frameIndex*/ ) > GetVideoFrameFn;
   typedef std::function< bool( float* /*leftCh*/, float* /*rightCh*/, int /*frameSize*/ ) > GetAudioFrameFn;

   VideoExporter( const std::string& outPath, const Params& inParams );
   virtual ~VideoExporter();

   void setGetVideoCallback( GetVideoFrameFn fn ) { _getVideo = fn; }
   void setGetAudioCallback( GetAudioFrameFn fn ) { _getAudio = fn; }

   void initialize();
   void exportEverything( int videoFrameCount );
   void completeExport();

protected:
   void initializeVideo( const AVCodec* codec );
   void initializeAudio( const AVCodec* codec );
   void initializeFrames();
   void initializePackets();

   int pushVideoUntilPacketFilled( int startFrameIndex );
   void pushAudioUntilPacketFilled();

   void cleanup();

   const std::string                   _path;
   const Params                        _inParams;
   Params                              _outParams;
   int64_t                             _ptsIncrement = 0LL;
   SwsContext*                         _swsContext = nullptr;
   AVFormatContext*                    _formatContext = nullptr;
   AVCodecContext*                     _videoCodecContext = nullptr;
   AVCodecContext*                     _audioCodecContext = nullptr;
   AVFrame*                            _colorConversionFrame = nullptr;
   AVFrame*                            _videoFrame = nullptr;
   AVFrame*                            _audioFrame = nullptr;
   AVPacket*                           _videoPacket = nullptr;
   AVPacket*                           _audioPacket = nullptr;
   GetVideoFrameFn                     _getVideo = nullptr;
   GetAudioFrameFn                     _getAudio = nullptr;
};