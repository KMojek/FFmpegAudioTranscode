#pragma once

extern "C"
{
#include <libavformat/avformat.h>
}

#include <cstdint>
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
   };

   VideoExporter( const std::string& outPath, const Params& inParams, const Params& outParams );
   virtual ~VideoExporter();

   bool initialize();
   bool deliverFrame( const uint8_t* frame, int frameSize );
   bool completeExport();

protected:
   const std::string    _path;
   const Params         _inParams;
   Params               _outParams;
};