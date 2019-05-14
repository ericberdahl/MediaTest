#include "sample_app.hpp"

#include "util.hpp"

#include <boost/exception/all.hpp>

/* ============================================================================================== */

void imageAvailable(sample::decoder& decoder, AImage* image)
{

}

int sample_main(int argc, char *argv[])
{
    try
    {
        sample::decoder decoder("/data/local/tmp/file1.mp4", &imageAvailable);
        decoder.start();
        while (!decoder.isDone())
        {
            // this space intentionally left blank
        }
    }
    catch (...)
    {
        LOGE("%s", boost::current_exception_diagnostic_information().c_str());
    }

    LOGI("MediaTest complete!!");

    return 0;
}
