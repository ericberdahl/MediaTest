#ifndef MEDIATEST_UTIL_HPP
#define MEDIATEST_UTIL_HPP

#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include <unistd.h>

#include <android/asset_manager.h>
#include <android/log.h>

#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/positioning.hpp>
#include <boost/iostreams/stream.hpp>

#if defined(NDEBUG) && defined(__GNUC__)
#define U_ASSERT_ONLY __attribute__((unused))
#else
#define U_ASSERT_ONLY
#endif

// Main entry point of samples
int sample_main(int argc, char *argv[]);

// Android specific definitions & helpers.
#define LOG(LEVEL, ...) ((void)__android_log_print(ANDROID_LOG_##LEVEL, "VK-SAMPLE", __VA_ARGS__))
#define LOGD(...) LOG(DEBUG, __VA_ARGS__)
#define LOGI(...) LOG(INFO, __VA_ARGS__)
#define LOGW(...) LOG(WARN, __VA_ARGS__)
#define LOGE(...) LOG(ERROR, __VA_ARGS__)
// Replace printf to logcat output.
#define printf(...) LOGD(__VA_ARGS__)

bool Android_process_command();

namespace android_utils {
    FILE* asset_fopen(const char* fname, const char* mode);

    // Helpder class to forward the cout/cerr output to logcat derived from:
    // http://stackoverflow.com/questions/8870174/is-stdcout-usable-in-android-ndk
    class LogBuffer : public std::streambuf {
    public:
        LogBuffer(android_LogPriority priority);

    private:
        static const std::int32_t    kBufferSize = 128;

        virtual int_type overflow(int_type c) override;

        virtual int_type sync() override;

    private:
        android_LogPriority priority_ = ANDROID_LOG_INFO;
        char                buffer_[kBufferSize];
    };

    class AssetSource {
    public:
        typedef char char_type;
        struct category
                : boost::iostreams::input_seekable,
                  boost::iostreams::device_tag,
                  boost::iostreams::closable_tag {
        };

        // Default constructor
        AssetSource() {}

        // Constructor taking a std:: string
        explicit AssetSource(const std::string &path,
                             std::ios::openmode mode = std::ios::in) {
            open(path, mode);
        }

        // Constructor taking a C-style string
        explicit AssetSource(const char *path,
                             std::ios::openmode mode = std::ios::in) {
            open(path, mode);
        }

        AssetSource(const AssetSource &other);

        ~AssetSource() {}

        bool is_open() const { return (nullptr != mAsset.get()); }

        void open(const std::string &path, std::ios::openmode mode = std::ios::in) {
            open(path.c_str(), mode);
        }

        void open(const char *path, std::ios::openmode mode = std::ios::in);

        std::streamsize read(char_type *s, std::streamsize n);

        std::streampos seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way);

        void close();

    private:
        std::shared_ptr<AAsset> mAsset;
    };

    typedef boost::iostreams::stream<AssetSource> iassetstream;
}

#endif // MEDIATEST_UTIL_HPP
