#pragma once

#include <functional>
#include <string>

extern "C"
{
   struct AVCodecContext;
   struct AVFormatContext;
   struct AVFrame;
   struct AVPacket;
}

struct AudioParams;

class AudioReaderDecoder
{
public:
   AudioReaderDecoder( const std::string& path );
   virtual ~AudioReaderDecoder();

   enum InitState
   {
      Ok, NoInit,
      FormatContextAllocFails, OpenFails, NoAudioStream, FindDecoderFails, CodecContextAllocFails, CodecOpenFails, FrameAllocFails, PacketAllocFails
   };

   InitState initialize();
   InitState initState() const { return _initState; }

   bool readAndDecode( std::function<void( const AVFrame * )> callback );

   bool getAudioParams( AudioParams& p );

protected:
   const std::string _path;
   InitState         _initState;
   int               _streamIndex;
   AVFormatContext*  _formatContext;
   AVCodecContext*   _codecContext;
   AVPacket*         _packet;
   AVFrame*          _frame;
};

