// FFmpegAudioTranscode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "WavUtil.h"

#include <string>
#include <vector>

#include "AudioLoader.h"
#include "InitFFmpeg.h"

int main( int argc, char **argv )
{
   if ( argc < 3 )
      return -1;

   std::string inputPath( argv[1] );
   std::string outputPath( argv[2] );

   InitFFmpeg();

   AudioLoader audioLoader( inputPath );
   if ( !audioLoader.loadAudioData() )
      return  -1;

   ::WriteWav( outputPath, audioLoader.processedAudio(), 44100 );

   return 0;
}