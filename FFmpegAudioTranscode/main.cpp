// FFmpegAudioTranscode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "AudioLoader.h"
#include "InitFFmpeg.h"
#include "VideoExporter.h"

#include <gtest/gtest.h>

extern "C"
{
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <iostream>
#include <string>

int main( int argc, char **argv )
{
   ::testing::InitGoogleTest( &argc, argv );

   InitFFmpeg();

   int result = RUN_ALL_TESTS();

#ifdef _DEBUG
   std::cout << "\nPress enter to continue...";
   std::cin.get();
#endif

   return result;
}


class FFmpegAudioTranscodeIntegrationTest : public ::testing::Test
{
protected:
   void SetUp() override
   {

   }

   void TearDown() override
   {

   }

   // We're hard-coded to resample to 44.1 kHz stereo and test files
   // were all generated in Audacity as 5-second long sine waves
   const uint64_t expectedSize = 44100 * 2 * 5;
};

TEST_F( FFmpegAudioTranscodeIntegrationTest, FiveSecondMP3_HasMatchingDecodedLength )
{
   const std::string testMediaPath( ".\\TestMedia\\five second mono sine wave.mp3" );

   AudioLoader audioLoader( testMediaPath );
   bool status = audioLoader.loadAudioData();
   EXPECT_TRUE( status );

   auto decodedAudioSize = audioLoader.processedAudio().size();
   EXPECT_EQ( decodedAudioSize, expectedSize );
}

auto sampleNearSilence = []( int16_t s )
{
   return std::abs( s ) < 110;
};
auto sampleNearPeak = []( int16_t s )
{
   return std::abs( s - 18000 ) <  50;
};
auto firstIndexWhere = []( const std::vector<int16_t>& samples, std::function<bool( int16_t )> lambda )
{
   auto iter = std::find_if( samples.cbegin(), samples.cend(), lambda );
   return std::distance( samples.cbegin(), iter );
};

bool valuesAlwaysIncresing( std::vector<int16_t>::const_iterator first, std::vector<int16_t>::const_iterator last )
{
   auto iter1 = first;
   auto iter2 = first; iter2 += 2;

   while ( iter2 != last )
   {
      if ( *iter1 >= *iter2 )
         return false;
      iter1 += 2; iter2 += 2;
   }
   return true;
}

TEST_F( FFmpegAudioTranscodeIntegrationTest, ThirtyTwoKHzMP3_DiscardsPrimingSamples )
{
   const std::string testMediaPath( ".\\TestMedia\\five second stereo 32kHz sine wave.mp3" );

   AudioLoader audioLoader( testMediaPath );
   audioLoader.loadAudioData();
   auto samples = audioLoader.processedAudio();

   // If priming samples are properly discarded, the samples should start out with smallish
   // positive values and gradually increase. With this particular sine wave, the peak should
   // be at t = 0.0005 seconds. With resampling to 44.1kHz, we should see the left and right
   // peak values right around index 44.
   ptrdiff_t index = firstIndexWhere( samples, sampleNearSilence );
   EXPECT_EQ( index, 0LL );

   index = firstIndexWhere( samples, sampleNearPeak );
   EXPECT_LT( std::abs( index - 44 ), 8 );

   EXPECT_TRUE( valuesAlwaysIncresing( samples.cbegin(), samples.cbegin() + index ) );
}

TEST_F( FFmpegAudioTranscodeIntegrationTest, FortyEightKHzMP3_DiscardsPrimingSamples )
{
   const std::string testMediaPath( ".\\TestMedia\\five second stereo 48kHz sine wave.mp3" );

   AudioLoader audioLoader( testMediaPath );
   audioLoader.loadAudioData();
   auto samples = audioLoader.processedAudio();

   // Same as above test; all this really tells us is that both 32 kHz and 48 kHz are
   // both resampled to 44.1 kHz in a semi-reaonsable way.
   ptrdiff_t index = firstIndexWhere( samples, sampleNearSilence );
   EXPECT_EQ( index, 0LL );

   index = firstIndexWhere( samples, sampleNearPeak );
   EXPECT_LT( std::abs( index - 44 ), 8 );

   EXPECT_TRUE( valuesAlwaysIncresing( samples.cbegin(), samples.cbegin() + index ) );
}

TEST_F( FFmpegAudioTranscodeIntegrationTest, WavImport_WorksAsExpected_WithoutPreviousHack )
{
   const std::string testMediaPath( ".\\TestMedia\\sine.wav" );

   AudioLoader audioLoader( testMediaPath );
   audioLoader.loadAudioData();

   auto decodedAudioSize = audioLoader.processedAudio().size();
   EXPECT_EQ( decodedAudioSize, expectedSize );
}


class VideoExporterIntegrationTest : public ::testing::Test
{
protected:
   void SetUp() override
   {
      tempPath = std::filesystem::temp_directory_path() / "out.mp4";
   }

   void TearDown() override
   {
      std::filesystem::remove( tempPath );
   }

   std::filesystem::path tempPath;

   // Arbitrary 128x96 RGB24 20-fps video with audio at 44.1 kHz
   const VideoExporter::Params params = { AV_PIX_FMT_RGB24, 128, 96, 20, 44100 };
   const int LengthInSeconds = 60;
   const int FrameCount = params.fps * LengthInSeconds;
};

TEST_F( VideoExporterIntegrationTest, VideoExporter_Initialize_And_CompleteExport_DoesNotThrow )
{
   VideoExporter exporter( tempPath.string(), params );

   EXPECT_NO_THROW( exporter.initialize() );

   EXPECT_NO_THROW( exporter.completeExport() );
}

TEST_F( VideoExporterIntegrationTest, VideoExporter_ExportDummySamplesSucceeds )
{
   VideoExporter exporter( tempPath.string(), params );

   exporter.initialize();

   EXPECT_NO_THROW( exporter.exportFrames( FrameCount ) );

   exporter.completeExport();
}

namespace
{
   int numCalls = 0;
   bool dummyGetAudio( float* leftCh, float* rightCh, int frameSize )
   {
      std::memset( leftCh, 0, frameSize * sizeof( float ) );
      std::memset( rightCh, 0, frameSize * sizeof( float ) );

      ++numCalls;
      return true;
   }
}

TEST_F( VideoExporterIntegrationTest, VideoExporter_ExportVideoOnlySucceeds )
{
   VideoExporter exporter( tempPath.string(), params, true );
   exporter.setGetAudioCallback( dummyGetAudio );

   exporter.initialize();
   exporter.exportFrames( FrameCount );
   exporter.completeExport();

   EXPECT_EQ( numCalls, 0 );
}
TEST_F( VideoExporterIntegrationTest, VideoExporter_ExportWithOddHeightSucceeds )
{
   const VideoExporter::Params myParams = { AV_PIX_FMT_RGB24, 904, 647, 20, 44100 };

   VideoExporter exporter( tempPath.string(), myParams, true );

   exporter.initialize();
   exporter.exportFrames( FrameCount );
   exporter.completeExport();

   // currently, need to manually look at output here :(
}