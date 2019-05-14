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

    decoder::decoder()
            : mAtInputEOS(false),
              mAtOutputEOS(false)
    {
        // this space intentionally left blank
    }

    decoder::decoder(const std::string& mediaFilePath,
                     imageAvailable_t imageAvailableFn)
            : decoder()
    {
        mImageAvailableFn = imageAvailableFn;

        // TODO mMediaFd will leak if an exception is thrown in the constructor
        mMediaFd = open(mediaFilePath.c_str(), O_RDONLY | O_CLOEXEC);
        if (mMediaFd < 0)
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("open")
                                           << boost::errinfo_errno(errno)
                                           << boost::errinfo_file_name(mediaFilePath) );
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

        LOGI("%s path:'%s' mediaSize:%jd", __FUNCTION__, mediaFilePath.c_str(), mediaSize);

        const auto format = selectVideoTrack();

        createImageReader(format.get());
        assert(mImageReader);

        createMediaCodec(format.get());
        assert(mMediaCodec);
    }

    decoder::~decoder()
    {
        if (mIOPollingThread.joinable())
        {
            mIOPollingThread.join();
        }

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

    void decoder::start()
    {
        assert(mMediaCodec);

        fail_media_error(AMediaCodec_start(mMediaCodec.get()),
                         "AMediaCodec_start");

        mIOPollingThread = std::thread(&decoder::ioPollingLoop, this);
    }

    void decoder::createImageReader(AMediaFormat* format)
    {
        assert(format && !mImageReader);

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
        mImageReader.reset(imageReader, AImageReader_delete);

        AImageReader_ImageListener imageListener = { this, &decoder::imageListenerCallback };
        fail_media_error(AImageReader_setImageListener(mImageReader.get(), &imageListener),
                         "AImageReader_setImageListener");
    }

    void decoder::createMediaCodec(AMediaFormat* format)
    {
        assert(format && !mMediaCodec && mImageReader);

        const char* mime = nullptr;
        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime))
        {
            BOOST_THROW_EXCEPTION( sample_error()
                                           << boost::errinfo_api_function("AMediaFormat_getString(AMEDIAFORMAT_KEY_MIME)") );
        }

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
                                               format,
                                               window,
                                               nullptr, // AMediaCrypto
                                               0), // flags
                         "AMediaCodec_configure");
    }

    std::shared_ptr<AMediaFormat> decoder::selectVideoTrack()
    {
        assert(mMediaExtractor);

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
        return std::shared_ptr<AMediaFormat>(result, AMediaFormat_delete);
    }

    void decoder::onInputBufferAvailable(AMediaCodec* codec,
                                         int32_t index)
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
        mAtInputEOS = AMediaExtractor_advance(mMediaExtractor.get());
        LOGI("%s bytesRead:%zd presentationTimeUs:%" PRId64 "atInputEOS:%s",
             __FUNCTION__,
             bytesRead,
             presentationTimeUs,
             mAtInputEOS ? "TRUE" : "FALSE");

        fail_media_error(AMediaCodec_queueInputBuffer(codec,
                                                      index,
                                                      0, // offset
                                                      bytesRead,
                                                      presentationTimeUs,
                                                      mAtInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0), // flags
                         "AMediaCodec_queueInputBuffer");
    }

    void decoder::onOutputBufferAvailable(AMediaCodec* codec,
                                          int32_t index,
                                          AMediaCodecBufferInfo *bufferInfo)
    {
        assert(codec == mMediaCodec.get());

        fail_media_error(AMediaCodec_releaseOutputBuffer(codec,
                                                         index,
                                                         true), // render the buffer to the bound surface
                         "AMediaCodec_releaseOutputBuffer");

        mAtOutputEOS = (0 != (bufferInfo->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM));
        LOGI("%s atOutputEOS:%s", __FUNCTION__, mAtOutputEOS ? "TRUE" : "FALSE");
    }

    void decoder::imageListenerCallback(void* userData, AImageReader* reader)
    {
        decoder* const self = static_cast<decoder*>(userData);

        try
        {
            AImage *image = nullptr;
            if (self->mFetchAllImages)
            {
                fail_media_error(AImageReader_acquireNextImage(reader, &image),
                                 "AImageReader_acquireNextImage");
            }
            else
            {
                fail_media_error(AImageReader_acquireLatestImage(reader, &image),
                                 "AImageReader_acquireLatestImage");
            }

            self->mImageAvailableFn(*self, image);
        }
        catch (...)
        {
            LOGE("%s", boost::current_exception_diagnostic_information().c_str());
        }
    }

    void decoder::ioPollingLoop()
    {
        assert(mMediaCodec);

        try
        {
            while (!isDone())
            {
                if (!mAtInputEOS)
                {
                    const std::int64_t timeoutUs = 1000;    // 1ms
                    const ssize_t bufferIndex = AMediaCodec_dequeueInputBuffer(mMediaCodec.get(),
                                                                               timeoutUs);
                    LOGI("%s inputBufferIndex:%zd", __FUNCTION__, bufferIndex);
                    if (bufferIndex >= 0)
                    {
                        onInputBufferAvailable(mMediaCodec.get(), bufferIndex);
                    }
                }

                if (!mAtOutputEOS)
                {
                    AMediaCodecBufferInfo bufferInfo = {};
                    const std::int64_t timeoutUs = 1000;    // 1ms
                    const ssize_t bufferIndex = AMediaCodec_dequeueOutputBuffer(mMediaCodec.get(),
                                                                                &bufferInfo,
                                                                                timeoutUs);
                    LOGI("%s outputBufferIndex:%zd", __FUNCTION__, bufferIndex);
                    if (bufferIndex >= 0)
                    {
                        onOutputBufferAvailable(mMediaCodec.get(),
                                                static_cast<int32_t>(bufferIndex),
                                                &bufferInfo);
                    }
                }
            }
        }
        catch(...)
        {
            LOGE("%s", boost::current_exception_diagnostic_information().c_str());
        }
    }
}