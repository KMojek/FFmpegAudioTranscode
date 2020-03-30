#include "stdafx.h"

#include "InitFFmpeg.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

void InitFFmpeg()
{
#if LIBAVFORMAT_VERSION_MAJOR < 58
   ::avcodec_register_all();
   ::av_register_all();
#endif
}