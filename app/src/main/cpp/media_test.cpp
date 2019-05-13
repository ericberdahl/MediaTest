#include "sample_app.hpp"

#include "util.hpp"

#include <boost/exception/all.hpp>

/* ============================================================================================== */

int sample_main(int argc, char *argv[])
{
    try
    {
        sample::app info;

        info.openMedia("/data/local/tmp/file1.mp4");
        info.startImageReader();

        bool inputEOSObserved = false;
        bool outputEOSObserved = false;
        while (!inputEOSObserved  || !outputEOSObserved)
        {
            if (!inputEOSObserved)
            {
                inputEOSObserved = info.advanceDecodeInput();
                LOGI("advanceDecodeInput: %s", !inputEOSObserved ? "more-data" : "at-EOS");
            }
            if (!outputEOSObserved)
            {
                outputEOSObserved = info.advanceDecodeOutput();
                LOGI("advanceDecodeOutput: %s", !outputEOSObserved ? "more-data" : "at-EOS");
            }
        }
    }
    catch (...)
    {
        LOGE("%s", boost::current_exception_diagnostic_information().c_str());
    }

    LOGI("MediaTest complete!!");

    return 0;
}
