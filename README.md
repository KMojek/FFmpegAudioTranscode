# FFmpegAudioTranscode
C++ wrapper around FFmpeg audio decoding and resampling. This is for when it makes sense to keep the decoded, possibly resampled, contents of an entire audio stream in memory... in other words, it does not support "on the fly" decoding of a file on disk. In it's current state, it is hard-coded to decode to 16-bit signed 44.1kHz stereo format. I'm currently buiiding on Windows in VS 2017 but the code should be cross-platform.

Reading, decoding, and resampling the contents of a file with an audio stream should be stupid-simple, like this:
```
   AudioReaderDecoderResampler audioLoader( "input.mp4" );
   if ( !audioLoader.loadAudioData() )
      return -1;

   const std::vector<int16_t>&  leftData( audioLoader.leftChannelData() );
   const std::vector<int16_t>& rightData( audioLoader.rightChannelData() );
```

Not shown here, but in case of loadAudioData() failure, there is a mechanism to drill down to the FFmpeg API call that led to the failure.
