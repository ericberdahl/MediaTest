//
// Created by Eric Berdahl on 2019-04-30.
//

#ifndef MEDIATEST_SAMPLE_APP_H
#define MEDIATEST_SAMPLE_APP_H

#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <boost/exception/error_info.hpp>

#include <memory>
#include <string>

namespace sample {

    typedef boost::error_info<struct tag_media_status,media_status_t> errinfo_media_status;
    typedef boost::error_info<struct tag_buffer_index,ssize_t> errinfo_buffer_index;

    class app
    {
    public:
        app();

        ~app();

        void    openMedia(const std::string &filepath);
        void    startImageReader();

        std::shared_ptr<AMediaFormat>   selectVideoTrack();

        bool    advanceDecodeInput();
        bool    advanceDecodeOutput();

        void    onInputBufferAvailable(AMediaCodec* codec, int32_t index);

        void    onOutputBufferAvailable(AMediaCodec* codec,
                                        int32_t index,
                                        AMediaCodecBufferInfo *bufferInfo);

        void    onFormatChanged(AMediaCodec *codec,
                                AMediaFormat *format);

        void    onError(AMediaCodec *codec,
                        media_status_t error,
                        int32_t actionCode,
                        const char *detail);

    private:
        static void imageListenerCallback(void* userData, AImageReader* reader);

        static void onAsyncInputBufferAvailableCallback(AMediaCodec* codec, void* userData, int32_t index);
        static void onAsyncOutputBufferAvailableCallback(AMediaCodec* codec,
                                                         void* userData,
                                                         int32_t index,
                                                         AMediaCodecBufferInfo *bufferInfo);
        static void onAsyncFormatChangedCallback(AMediaCodec *codec,
                                                 void* userData,
                                                 AMediaFormat *format);
        static void onAsyncErrorCallback(AMediaCodec *codec,
                                         void* userData,
                                         media_status_t error,
                                         int32_t actionCode,
                                         const char *detail);

    private:
        int                                 mMediaFd = -1;
        std::shared_ptr<AMediaExtractor>    mMediaExtractor;
        std::shared_ptr<AImageReader>       mImageReader;
        std::shared_ptr<AMediaCodec>        mMediaCodec;
    };
}


#endif //MEDIATEST_SAMPLE_APP_H
