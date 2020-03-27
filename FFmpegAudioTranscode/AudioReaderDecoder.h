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

enum class AudioReaderDecoderInitState
{
   Ok, NoInit,
   FormatContextAllocFails, OpenFails, NoAudioStream, FindDecoderFails, CodecContextAllocFails, CodecOpenFails, FrameAllocFails, PacketAllocFails
};

class AudioReaderDecoder
{
public:
   AudioReaderDecoder( const std::string& path );
   virtual ~AudioReaderDecoder();

   AudioReaderDecoderInitState initialize();
   AudioReaderDecoderInitState initState() const { return _initState; }

   bool readAndDecode( std::function<void( const AVFrame * )> callback );

   bool getAudioParams( AudioParams& p );

protected:
   const std::string             _path;
   AudioReaderDecoderInitState   _initState;
   int                           _streamIndex;
   AVFormatContext*              _formatContext;
   AVCodecContext*               _codecContext;
   AVPacket*                     _packet;
   AVFrame*                      _frame;
};

