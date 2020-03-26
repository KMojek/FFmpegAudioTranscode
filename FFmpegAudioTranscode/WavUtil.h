#pragma once

#include "AudioParams.h"

#include <cstdint>
#include <string>
#include <vector>

extern bool WriteWav( const std::string& path, const std::vector< std::vector<int16_t> >& data, int rate );

extern bool ReadWavAudioParams( const std::string& path, AudioParams& params );
