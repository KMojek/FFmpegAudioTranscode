#include "stdafx.h"

#include "WavWriter.h"

#include <cassert>
#include <fstream>

struct WAVHeader
{
   WAVHeader() {}
   WAVHeader( int fileLength, int numChannels, int rate )
      : sampleRate( rate )
   {
      SetFileLength( fileLength );
      channels = numChannels;
      compressionCode = 1;
      bitsPerSample = 16;
      blockAlign = bitsPerSample * channels / 8;
      bytesPerSecond = sampleRate * blockAlign;
   }
   void SetFileLength( int fileLength )
   {
      lenMinus8 = fileLength - 8;
      dataChunkSize = fileLength - 44;
   }

   char riff[4] = { 'R', 'I', 'F', 'F' };
   int lenMinus8;
   char wave[4] = { 'W', 'A', 'V', 'E' };
   char fmt[4] = { 'f', 'm', 't', ' ' };
   int fmtChunkSize = 16;
   short compressionCode = 1;
   short channels = 2;
   int sampleRate = 44100;
   int bytesPerSecond;
   short blockAlign;
   short bitsPerSample = 16;
   char data[4] = { 'd', 'a', 't', 'a' };
   int dataChunkSize;
};

class WAVFileWriter
{
public:
   WAVFileWriter( const std::string& filename, int numChannels, int rate )
      : _File( filename, std::ofstream::binary )
      , _WavHeader( 0, numChannels, rate )
   {
      static_assert( sizeof( _WavHeader ) == 44, "_WavHeader size is not correct" );
      _File.write( (const char*)&_WavHeader, sizeof( _WavHeader ) );
   }
   ~WAVFileWriter()
   {
      std::streampos fileLen = _File.tellp();
      _File.seekp( 0 );
      WAVHeader hdr = _WavHeader;
      hdr.SetFileLength( (int)fileLen );
      _File.write( (const char*)&hdr, sizeof( hdr ) ); // overwrite header with real data
   }
   void WriteSample( int16_t x )
   {
      _File.write( (const char *)&x, 2 );
   }

protected:
   std::ofstream  _File;
   WAVHeader      _WavHeader;
};

bool WriteWav( const std::string& path, const std::vector< std::vector<int16_t> >& data, int rate )
{
   WAVFileWriter writer( path, 2, rate );
   std::vector<int16_t>::size_type n = data[0].size();
   for ( std::vector<int16_t>::size_type i = 0; i < n; ++i )
   {
      writer.WriteSample( data[0][i] );
      writer.WriteSample( data[1][i] );
   }

   return true;
}