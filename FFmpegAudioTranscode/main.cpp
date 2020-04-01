// FFmpegAudioTranscode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <iostream>
#include <string>

#include <gtest/gtest.h>

#include "AudioLoader.h"
#include "InitFFmpeg.h"


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
};

TEST_F( FFmpegAudioTranscodeIntegrationTest, FiveSecondMP3_HasMatchingDecodedLength )
{
   const std::string testMediaPath( ".\\TestMedia\\five second mono sine wave.mp3" );
   const uint64_t expectedSize = 44100 * 2 * 5; // hard-coded default in AudioLoader

   AudioLoader audioLoader( testMediaPath );
   bool status = audioLoader.loadAudioData();
   EXPECT_TRUE( status );

   auto decodedAudioSize = audioLoader.processedAudio().size();
   EXPECT_EQ( decodedAudioSize, expectedSize );
}