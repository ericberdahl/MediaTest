//
// Created by Eric Berdahl on 2019-04-30.
//

#include "sample_app.hpp"

#include "util.hpp"

#include <boost/exception/all.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace {
    using namespace sample;

    std::shared_ptr<AMediaCodec> createMediaCodec(AMediaFormat* format, ANativeWindow* window)
    {
        const char* mime = nullptr;
        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime))
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaFormat_getString(AMEDIAFORMAT_KEY_MIME)") );
        }

        std::shared_ptr<AMediaCodec> result(AMediaCodec_createDecoderByType(mime), AMediaCodec_delete);
        if (!result)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaCodec_createDecoderByType") );
        }

        fail_media_error(AMediaCodec_configure(result.get(),
                                               format,
                                               window,
                                               nullptr, // AMediaCrypto
                                               0), // flags
                         "AMediaCodec_configure");

        return result;
    }
}

namespace sample {

    void fail_media_error(media_status_t status, const char* apiFunction)
    {
        if (status != AMEDIA_OK)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function(apiFunction)
                                           << sample::errinfo_media_status(status) );
        }
    }

    std::shared_ptr<AImageReader> createImageReader(AMediaFormat* format)
    {
        std::int32_t    width = 0;
        std::int32_t    height = 0;
        if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width) &&
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height))
        {
            LOGI("%s width=%d height=%d", __FUNCTION__, width, height);
        }

        const std::int32_t  maxImageCount = 5;

        AImageReader* imageReader;
        fail_media_error(AImageReader_newWithUsage(width,
                                                   height,
                                                   AIMAGE_FORMAT_YUV_420_888,
                                                   AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                                                   maxImageCount,
                                                   &imageReader),
                         "AImageReader_newWithUsage");

        return std::shared_ptr<AImageReader>(imageReader, AImageReader_delete);
    }

    std::shared_ptr<AMediaExtractor> createMediaExtractor(int fd)
    {
        std::shared_ptr<AMediaExtractor> result(AMediaExtractor_new(), AMediaExtractor_delete);
        if (!result)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaExtractor_new") );
        }

        const off64_t mediaSize = lseek64(fd, 0, SEEK_END);
        lseek64(fd, 0, SEEK_SET);
        fail_media_error(
                AMediaExtractor_setDataSourceFd(result.get(), fd, 0, mediaSize),
                "AMediaExtractor_setDataSourceFd");

        return result;
    }

    std::shared_ptr<AMediaFormat> selectVideoTrack(AMediaExtractor* extractor)
    {
        LOGI("%s BEGIN", __FUNCTION__);

        const std::size_t numTracks = AMediaExtractor_getTrackCount(extractor);
        LOGI("%s numTracks:%zd", __FUNCTION__, numTracks);

        AMediaFormat* result = nullptr;
        for (std::size_t track = 0; track < numTracks; ++track)
        {
            AMediaFormat* const format = AMediaExtractor_getTrackFormat(extractor, track);

            const char* mime = nullptr;
            if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) &&
                0 == std::strncmp(mime, "video/", 6))
            {
                AMediaExtractor_selectTrack(extractor, track);
                result = format;
                LOGI("%s trackNum:%zd format:'%s'", __FUNCTION__, track, AMediaFormat_toString(result));
                break;
            }
        }

        LOGI("%s END", __FUNCTION__);
        return std::shared_ptr<AMediaFormat>(result, AMediaFormat_delete);
    }

    std::tuple<bool, std::size_t, std::uint64_t> readSampleData(AMediaExtractor* extractor,
                                                                void* buffer,
                                                                size_t capacity)
    {
        const ssize_t bytesRead = AMediaExtractor_readSampleData(extractor,
                                                                 static_cast<std::uint8_t*>(buffer),
                                                                 capacity);
        const std::int64_t presentationTimeUs = AMediaExtractor_getSampleTime(extractor);

        const bool moreDataAvailable = (bytesRead >= 0 && AMediaExtractor_advance(extractor));

        return std::make_tuple(moreDataAvailable,
                               size_t( std::max(bytesRead, ssize_t(0)) ),
                               std::uint64_t( std::abs( presentationTimeUs ) ));
    }

    decoder::decoder()
            : mAtInputEOS(false),
              mAtOutputEOS(false)
    {
        // this space intentionally left blank
    }

    decoder::decoder(AMediaFormat* format,
                     readSampleData_t readSampleData,
                     ANativeWindow* window)
            : decoder()
    {
        mReadSampleDataFn = readSampleData;

        mMediaCodec = createMediaCodec(format, window);

        AMediaCodecOnAsyncNotifyCallback callbacks;
        callbacks.onAsyncError = &decoder::asyncErrorCallback;
        callbacks.onAsyncFormatChanged = &decoder::asyncFormatChangedCallback;
        callbacks.onAsyncInputAvailable= &decoder::asyncInputAvailableCallback;
        callbacks.onAsyncOutputAvailable = &decoder::asyncOutputAvailableCallback;
        fail_media_error(AMediaCodec_setAsyncNotifyCallback(mMediaCodec.get(), callbacks, this),
                         "AMediaCodec_setAsyncNotifyCallback");
    }

    decoder::~decoder()
    {
        assert(isDone());

        if (mIOThread.joinable())
        {
            mIOThread.join();
        }

        if (mMediaCodec)
        {
            (void) AMediaCodec_stop(mMediaCodec.get());
        }
    }

    void decoder::start()
    {
        assert(mMediaCodec);

        mIOThread = std::thread(&decoder::ioThread, this);
        fail_media_error(AMediaCodec_start(mMediaCodec.get()),
                         "AMediaCodec_start");
    }

    void decoder::onInputAvailable(AMediaCodec* codec,
                                   int32_t index)
    {
        assert(codec == mMediaCodec.get() && codec);

        LOGI("%s index:%d", __FUNCTION__, index);

        std::size_t     bufferCapacity = 0;
        std::uint8_t*   buffer = AMediaCodec_getInputBuffer(codec,
                                                            index,
                                                            &bufferCapacity);
        if (!buffer)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaCodec_getInputBuffer") );
        }
        LOGI("%s bufferCapacity:%zd", __FUNCTION__, bufferCapacity);

        ssize_t bytesRead = 0;
        std::int64_t presentationTimeUs = 0;
        bool moreDataAvailable = true;
        std::tie(moreDataAvailable, bytesRead, presentationTimeUs) = mReadSampleDataFn(*this,
                                                                                       buffer,
                                                                                       bufferCapacity);

        LOGI("%s bytesRead:%zd presentationTimeUs:%" PRId64 " moreData:%s",
             __FUNCTION__,
             bytesRead,
             presentationTimeUs,
             moreDataAvailable ? "TRUE" : "FALSE");

        fail_media_error(AMediaCodec_queueInputBuffer(codec,
                                                      index,
                                                      0, // offset
                                                      bytesRead,
                                                      presentationTimeUs,
                                                      moreDataAvailable ? 0
                                                                        : AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM), // flags
                         "AMediaCodec_queueInputBuffer");

        mAtInputEOS = !moreDataAvailable;
    }

    void decoder::onOutputAvailable(AMediaCodec* codec,
                                    int32_t index,
                                    AMediaCodecBufferInfo *bufferInfo)
    {
        assert(codec == mMediaCodec.get());

        fail_media_error(AMediaCodec_releaseOutputBuffer(codec,
                                                         index,
                                                         true), // render the buffer to the bound surface
                         "AMediaCodec_releaseOutputBuffer");

        mAtOutputEOS = (0 != (bufferInfo->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM));

        LOGI("%s index:%d  atOutputEOS:%s count:%u",
             __FUNCTION__,
             index,
             mAtOutputEOS ? "TRUE" : "FALSE",
             mNumOutputBuffers++);
    }

    void decoder::onFormatChanged(AMediaCodec *codec,
                                  AMediaFormat *format)
    {
        assert(codec == mMediaCodec.get() && codec && format);
        LOGE("%s { %s }", __FUNCTION__, AMediaFormat_toString(format));
    }

    void decoder::onError(AMediaCodec *codec,
                          media_status_t error,
                          int32_t actionCode,
                          const char *detail)
    {
        assert(codec == mMediaCodec.get() && codec && detail);
        LOGE("%s %d %d '%s", __FUNCTION__, error, actionCode, detail);
    }

    void decoder::ioThread()
    {
        while (!isDone())
        {
            auto task = mIOQueue.pop();
            task();
        }
    }

    void decoder::asyncInputAvailableCallback(AMediaCodec* codec,
                                              void* userData,
                                              int32_t index)
    {
        decoder* const self = static_cast<decoder*>(userData);

        self->mIOQueue.push([self, codec, index]() { self->onInputAvailable(codec, index); });
    }

    void decoder::asyncOutputAvailableCallback(AMediaCodec* codec,
                                               void* userData,
                                               int32_t index,
                                               AMediaCodecBufferInfo *bufferInfo)
    {
        decoder* const self = static_cast<decoder*>(userData);

        AMediaCodecBufferInfo bufferInfoCopy = *bufferInfo;

        self->mIOQueue.push([self, codec, index, bufferInfoCopy]() {
            AMediaCodecBufferInfo bc = bufferInfoCopy;
            self->onOutputAvailable(codec, index, &bc);
        });
    }

    void decoder::asyncFormatChangedCallback(AMediaCodec *codec,
                                             void* userData,
                                             AMediaFormat *format)
    {
        decoder* const self = static_cast<decoder*>(userData);

        self->mIOQueue.push([self, codec, format]() { self->onFormatChanged(codec, format); });
    }

    void decoder::asyncErrorCallback(AMediaCodec *codec,
                                     void* userData,
                                     media_status_t error,
                                     int32_t actionCode,
                                     const char *detail)
    {
        decoder* const self = static_cast<decoder*>(userData);

        self->mIOQueue.push([self, codec, error, actionCode, detail]() {
            self->onError(codec, error, actionCode, detail);
        });
    }
}