//
// Created by Eric Berdahl on 2019-04-30.
//

#ifndef MEDIATEST_SAMPLE_APP_H
#define MEDIATEST_SAMPLE_APP_H

#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <boost/exception/error_info.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace sample {

    typedef boost::error_info<struct tag_media_status,media_status_t> errinfo_media_status;
    typedef boost::error_info<struct tag_buffer_index,ssize_t> errinfo_buffer_index;

    class decoder
    {
    public:
        typedef std::function<void(decoder&, AImage*)>    imageAvailable_t;

        decoder();
        decoder(const std::string& mediaFilePath, imageAvailable_t imageAvailableFn);
        decoder(const decoder& other) = delete;
        ~decoder();

        decoder& operator=(const decoder& other) = delete;

        void    start();

        bool    isInputDone() const { return mAtInputEOS; }
        bool    isOutputDone() const { return mAtOutputEOS; }
        bool    isDone() const { return isInputDone() && isOutputDone(); }

    private:
        void    ioPollingLoop();

    private:
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
        void                            createImageReader(AMediaFormat* format);
        void                            createMediaCodec(AMediaFormat* format);
        std::shared_ptr<AMediaFormat>   selectVideoTrack();

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
        bool                                mFetchAllImages = false;
        int                                 mMediaFd        = -1;
        imageAvailable_t                    mImageAvailableFn;
        std::shared_ptr<AMediaExtractor>    mMediaExtractor;
        std::shared_ptr<AImageReader>       mImageReader;
        std::shared_ptr<AMediaCodec>        mMediaCodec;
        std::atomic<bool>                   mAtInputEOS;
        std::atomic<bool>                   mAtOutputEOS;
        std::thread                         mIOPollingThread;
    };

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
        int                                 mMediaFd        = -1;
        std::shared_ptr<AMediaExtractor>    mMediaExtractor;
        std::shared_ptr<AImageReader>       mImageReader;
        std::shared_ptr<AMediaCodec>        mMediaCodec;
        bool                                mAtInputEOS     = false;
        bool                                mAtOutputEOS    = false;
    };
}


#endif //MEDIATEST_SAMPLE_APP_H
