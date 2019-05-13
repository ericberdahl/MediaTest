//
// Created by Eric Berdahl on 2019-04-30.
//

#include "sample_app.hpp"

#include "util.hpp"

#include <boost/exception/all.hpp>

#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace {
    struct sample_error: virtual boost::exception, virtual std::exception { };


    void fail_media_error(media_status_t status, const char* apiFunction)
    {
        if (status != AMEDIA_OK)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                        << boost::errinfo_api_function(apiFunction)
                                        << sample::errinfo_media_status(status) );
        }
    }
}

namespace sample {

    app::app()
    {

    }

    app::~app()
    {
        if (mMediaCodec)
        {
            (void) AMediaCodec_stop(mMediaCodec.get());
        }

        if (mMediaFd > 0)
        {
            close(mMediaFd);
            mMediaFd = -1;
        }
    }

    void app::openMedia(const std::string &filepath)
    {
        // TODO throw if the file is already open
        mMediaFd = open(filepath.c_str(), O_RDONLY | O_CLOEXEC);
        if (mMediaFd < 0)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                        << boost::errinfo_api_function("open")
                                        << boost::errinfo_errno(errno)
                                        << boost::errinfo_file_name(filepath) );
        }

        mMediaExtractor.reset(AMediaExtractor_new(), AMediaExtractor_delete);
        if (!mMediaExtractor)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                        << boost::errinfo_api_function("AMediaExtractor_new") );
        }

        const off64_t mediaSize = lseek64(mMediaFd, 0, SEEK_END);
        lseek64(mMediaFd, 0, SEEK_SET);
        fail_media_error(
                AMediaExtractor_setDataSourceFd(mMediaExtractor.get(), mMediaFd, 0, mediaSize),
                "AMediaExtractor_setDataSourceFd");

        LOGI("%s path:'%s' mediaSize:%jd", __FUNCTION__, filepath.c_str(), mediaSize);
    }

    std::shared_ptr<AMediaFormat> app::selectVideoTrack()
    {
        if (!mMediaExtractor)
        {
            BOOST_THROW_EXCEPTION( sample_error() );
        }

        LOGI("%s BEGIN", __FUNCTION__);

        const std::size_t numTracks = AMediaExtractor_getTrackCount(mMediaExtractor.get());
        LOGI("%s numTracks:%zd", __FUNCTION__, numTracks);

        AMediaFormat* result = nullptr;
        for (std::size_t track = 0; track < numTracks; ++track)
        {
            AMediaFormat* const format = AMediaExtractor_getTrackFormat(mMediaExtractor.get(), track);

            const char* mime = nullptr;
            if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) &&
                0 == std::strncmp(mime, "video/", 6))
            {
                AMediaExtractor_selectTrack(mMediaExtractor.get(), track);
                result = format;
                LOGI("%s trackNum:%zd format:'%s'", __FUNCTION__, track, AMediaFormat_toString(result));
                break;
            }
        }

        LOGI("%s END", __FUNCTION__);
        return std::shared_ptr<AMediaFormat>(result, [](AMediaFormat* mf) { AMediaFormat_delete(mf); });
    }

    void app::startImageReader()
    {
        if (mImageReader || mMediaCodec)
        {
            BOOST_THROW_EXCEPTION( sample_error() );
        }

        LOGI("%s BEGIN", __FUNCTION__);

        const auto format = selectVideoTrack();

        std::int32_t    width = 0;
        std::int32_t    height = 0;
        const char*     mime = nullptr;
        if (AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH, &width) &&
            AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT, &height) &&
            AMediaFormat_getString(format.get(), AMEDIAFORMAT_KEY_MIME, &mime))
        {
            LOGI("%s width=%d height=%d mime='%s'", __FUNCTION__, width, height, mime);
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
        mImageReader.reset(imageReader, AImageReader_delete);

        AImageReader_ImageListener imageListener = { this, &app::imageListenerCallback };
        fail_media_error(AImageReader_setImageListener(mImageReader.get(), &imageListener),
                         "AImageReader_setImageListener");

        mMediaCodec.reset(AMediaCodec_createDecoderByType(mime), AMediaCodec_delete);
        if (!mMediaCodec)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                        << boost::errinfo_api_function("AMediaCodec_createDecoderByType") );
        }

        ANativeWindow* window = nullptr;
        fail_media_error(AImageReader_getWindow(mImageReader.get(), &window),
                         "AImageReader_getWindow");

        fail_media_error(AMediaCodec_configure(mMediaCodec.get(),
                                               format.get(),
                                               window,
                                               nullptr, // AMediaCrypto
                                               0), // flags
                        "AMediaCodec_configure");

        fail_media_error(AMediaCodec_start(mMediaCodec.get()),
                         "AMediaCodec_start");

        LOGI("%s END", __FUNCTION__);
    }

    bool app::advanceDecodeInput()
    {
        if (!mMediaCodec)
        {
            BOOST_THROW_EXCEPTION(sample_error());
        }

        LOGI("%s BEGIN", __FUNCTION__);

        const std::int64_t  timeoutUs = 0;
        const ssize_t       bufferIndex = AMediaCodec_dequeueInputBuffer(mMediaCodec.get(), timeoutUs);
        if (bufferIndex < 0)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaCodec_dequeueInputBuffer")
                                           << errinfo_buffer_index(bufferIndex) );
        }
        LOGI("%s bufferIndex:%zd", __FUNCTION__, bufferIndex);

        std::size_t     bufferCapacity = 0;
        std::uint8_t*   buffer = AMediaCodec_getInputBuffer(mMediaCodec.get(),
                                                            bufferIndex,
                                                            &bufferCapacity);
        if (!buffer)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaCodec_getInputBuffer") );
        }
        LOGI("%s bufferCapacity:%zd", __FUNCTION__, bufferCapacity);

        const ssize_t bytesRead = AMediaExtractor_readSampleData(mMediaExtractor.get(),
                                                                 buffer,
                                                                 bufferCapacity);
        const std::int64_t presentationTimeUs = AMediaExtractor_getSampleTime(mMediaExtractor.get());
        assert(0 <= presentationTimeUs);
        const bool atEOS = AMediaExtractor_advance(mMediaExtractor.get());
        LOGI("%s bytesRead:%zd presentationTimeUs:%" PRId64 "atEOS:%s",
                 __FUNCTION__,
                 bytesRead,
                 presentationTimeUs,
                 atEOS ? "TRUE" : "FALSE");

        fail_media_error(AMediaCodec_queueInputBuffer(mMediaCodec.get(),
                                                      bufferIndex,
                                                      0, // offset
                                                      bytesRead,
                                                      presentationTimeUs,
                                                      atEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0), // flags
                         "AMediaCodec_queueInputBuffer");

        LOGI("%s END", __FUNCTION__);
        return atEOS;    // more to read
    }

    bool app::advanceDecodeOutput()
    {
        // TODO check that we have a MediaCodec
        LOGI("%s BEGIN", __FUNCTION__);

        AMediaCodecBufferInfo bufferInfo = {};
        const std::int64_t timeoutUs = 0;
        const ssize_t bufferIndex = AMediaCodec_dequeueOutputBuffer(mMediaCodec.get(),
                                                                    &bufferInfo,
                                                                    timeoutUs);
        LOGI("%s bufferIndex:%zd", __FUNCTION__, bufferIndex);
        if (bufferIndex >= 0)
        {
            onOutputBufferAvailable(mMediaCodec.get(), bufferIndex, &bufferInfo);
        }

        LOGI("%s END", __FUNCTION__);
        return (0 != (bufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM));
    }

    void app::imageListenerCallback(void* userData, AImageReader* reader)
    {
        LOGI("%s", __FUNCTION__);
    }

    void app::onAsyncInputBufferAvailableCallback(AMediaCodec* codec, void* userData, int32_t index)
    {
        static_cast<app*>(userData)->onInputBufferAvailable(codec, index);
    }

    void onAsyncOutputBufferAvailableCallback(AMediaCodec* codec,
                                              void* userData,
                                              int32_t index,
                                              AMediaCodecBufferInfo *bufferInfo)
    {
        static_cast<app*>(userData)->onOutputBufferAvailable(codec, index, bufferInfo);
    }

    void onAsyncFormatChangedCallback(AMediaCodec *codec,
                                      void* userData,
                                      AMediaFormat *format)
    {
        static_cast<app*>(userData)->onFormatChanged(codec, format);
    }

    void onAsyncErrorCallback(AMediaCodec *codec,
                              void* userData,
                              media_status_t error,
                              int32_t actionCode,
                              const char *detail)
    {
        static_cast<app*>(userData)->onError(codec, error, actionCode, detail);
    }


    void app::onInputBufferAvailable(AMediaCodec* codec, int32_t index)
    {
        assert(codec == mMediaCodec.get());

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

        const ssize_t bytesRead = AMediaExtractor_readSampleData(mMediaExtractor.get(),
                                                                 buffer,
                                                                 bufferCapacity);
        const std::int64_t presentationTimeUs = AMediaExtractor_getSampleTime(mMediaExtractor.get());
        assert(0 <= presentationTimeUs);
        const bool atEOS = AMediaExtractor_advance(mMediaExtractor.get());
        LOGI("%s bytesRead:%zd presentationTimeUs:%" PRId64 "atEOS:%s",
             __FUNCTION__,
             bytesRead,
             presentationTimeUs,
             atEOS ? "TRUE" : "FALSE");

        fail_media_error(AMediaCodec_queueInputBuffer(codec,
                                                      index,
                                                      0, // offset
                                                      bytesRead,
                                                      presentationTimeUs,
                                                      atEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0), // flags
                         "AMediaCodec_queueInputBuffer");
    }

    void app::onOutputBufferAvailable(AMediaCodec* codec,
                                      int32_t index,
                                      AMediaCodecBufferInfo *bufferInfo)
    {
        assert(codec == mMediaCodec.get());

        fail_media_error(AMediaCodec_releaseOutputBuffer(codec,
                                                         index,
                                                         true), // render the buffer to the bound surface
                         "AMediaCodec_releaseOutputBuffer");

        const bool atEOS = (0 != (bufferInfo->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM));
        LOGI("%s atEOS:%s", __FUNCTION__, atEOS ? "TRUE" : "FALSE");

    }

    void app::onFormatChanged(AMediaCodec *codec,
                              AMediaFormat *format)
    {
        assert(codec == mMediaCodec.get());

        LOGI("%s", __FUNCTION__);
    }

    void app::onError(AMediaCodec *codec,
                      media_status_t error,
                      int32_t actionCode,
                      const char *detail)
    {
        assert(codec == mMediaCodec.get());

        LOGI("%s", __FUNCTION__);
    }
}