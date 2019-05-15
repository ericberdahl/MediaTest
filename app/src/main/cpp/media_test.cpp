#include "sample_app.hpp"

#include "util.hpp"

#include <boost/exception/all.hpp>

#include <fcntl.h>

/* ============================================================================================== */
namespace {
    unsigned int gNumImages = 0;
}

void imageAvailable(void* userData, AImageReader* reader)
{
    try
    {
        AImage *image = nullptr;
        sample::fail_media_error(AImageReader_acquireNextImage(reader, &image),
                                 "AImageReader_acquireNextImage");

        LOGI("%s received image #%u", __FUNCTION__, gNumImages++);
        AImage_delete(image);
    }
    catch (...)
    {
        LOGE("%s", boost::current_exception_diagnostic_information().c_str());
    }
}

int sample_main(int argc, char *argv[])
{
    const char* const mediaFilePath = "/data/local/tmp/file1.mp4";

    try
    {
        // TODO mediaFd will leak if an exception is thrown
        const auto mediaFd = open(mediaFilePath, O_RDONLY | O_CLOEXEC);
        if (mediaFd < 0)
        {
            BOOST_THROW_EXCEPTION( sample::sample_error()
                                           << boost::errinfo_api_function("open")
                                           << boost::errinfo_errno(errno)
                                           << boost::errinfo_file_name(mediaFilePath) );
        }

        const auto mediaExtractor = sample::createMediaExtractor(mediaFd);
        const auto format = sample::selectVideoTrack(mediaExtractor.get());
        const auto readSampleData = std::bind(&sample::readSampleData,
                                              mediaExtractor.get(),
                                              std::placeholders::_2,
                                              std::placeholders::_3);

        const auto imageReader = sample::createImageReader(format.get());

        AImageReader_ImageListener imageListener = { nullptr, &imageAvailable };
        sample::fail_media_error(AImageReader_setImageListener(imageReader.get(), &imageListener),
                                 "AImageReader_setImageListener");

        ANativeWindow* window = nullptr;
        sample::fail_media_error(AImageReader_getWindow(imageReader.get(), &window),
                                 "AImageReader_getWindow");

        sample::decoder decoder(format.get(), readSampleData, window);
        decoder.start();
        while (!decoder.isDone())
        {
            // this space intentionally left blank
        }

        close(mediaFd);
    }
    catch (...)
    {
        LOGE("%s", boost::current_exception_diagnostic_information().c_str());
    }

    LOGI("MediaTest complete!!");

    return 0;
}
