// FFmpegAudioTranscode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "WavWriter.h"

#include <string>
#include <vector>

#include "AudioReaderDecoderResampler.h"

int main( int argc, char **argv )
{
   if ( argc < 3 )
      return -1;

   std::string inputPath( argv[1] );
   std::string outputPath( argv[2] );

   AudioReaderDecoderResampler audioLoader( inputPath );
   if ( !audioLoader.loadAudioData() )
      return  -1;

   std::vector< std::vector<int16_t> > data = {
      audioLoader.leftChannelData(),
      audioLoader.rightChannelData()
   };
   ::WriteWav( outputPath, data, 44100 );

   return 0;
}