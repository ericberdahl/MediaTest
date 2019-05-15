//
// Created by Eric Berdahl on 2019-04-30.
//

#ifndef MEDIATEST_SAMPLE_APP_H
#define MEDIATEST_SAMPLE_APP_H

#include "StopWatch.hpp"

#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <boost/exception/exception.hpp>
#include <boost/exception/error_info.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

namespace sample {

    typedef boost::error_info<struct tag_media_status,media_status_t> errinfo_media_status;
    typedef boost::error_info<struct tag_buffer_index,ssize_t> errinfo_buffer_index;

    struct sample_error: virtual boost::exception, virtual std::exception { };

    void fail_media_error(media_status_t status, const char* apiFunction);

    std::tuple<bool, std::size_t, std::uint64_t> readSampleData(AMediaExtractor* extractor,
                                                                void* buffer,
                                                                size_t capacity);

    std::shared_ptr<AMediaExtractor> createMediaExtractor(int fd);

    std::shared_ptr<AMediaFormat> selectVideoTrack(AMediaExtractor* extractor);

    std::shared_ptr<AImageReader> createImageReader(AMediaFormat* format);

    template <typename T>
    class pc_queue
    {
    public:
        pc_queue() {}
        ~pc_queue() {}

        void push(T elem)
        {
            bool needsNotify = false;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                needsNotify = mQueue.empty();
                mQueue.push(std::move(elem));
            }

            if (needsNotify)
            {
                mQueueNotEmptyCondition.notify_one();
            }
        }

        T pop()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            if (mQueue.empty())
            {
                mQueueNotEmptyCondition.wait(lock, [this]() { return !mQueue.empty(); });
            }
            auto result = mQueue.front();
            mQueue.pop();
            return result;
        }

    private:
        std::mutex              mMutex;
        std::condition_variable mQueueNotEmptyCondition;
        std::queue<T>           mQueue;
    };

    class decoder
    {
    public:
        typedef std::function<std::tuple<bool,std::size_t,std::uint64_t>(decoder&, void*, size_t)>    readSampleData_t;

        decoder();

        decoder(AMediaFormat* format,
                readSampleData_t readSampleData,
                ANativeWindow* window);

        decoder(const decoder& other) = delete;
        ~decoder();

        decoder& operator=(const decoder& other) = delete;

        void    start();

        bool    isInputDone() const { return mAtInputEOS; }
        bool    isOutputDone() const { return mAtOutputEOS; }
        bool    isDone() const { return isInputDone() && isOutputDone(); }

    private:
        void    ioThread();

    private:
        void    onInputAvailable(AMediaCodec* codec, int32_t index);

        void    onOutputAvailable(AMediaCodec* codec,
                                  int32_t index,
                                  AMediaCodecBufferInfo *bufferInfo);

        void    onFormatChanged(AMediaCodec *codec,
                                AMediaFormat *format);

        void    onError(AMediaCodec *codec,
                        media_status_t error,
                        int32_t actionCode,
                        const char *detail);

    private:
        static void asyncInputAvailableCallback(AMediaCodec* codec,
                                                void* userData,
                                                int32_t index);
        static void asyncOutputAvailableCallback(AMediaCodec* codec,
                                                 void* userData,
                                                 int32_t index,
                                                 AMediaCodecBufferInfo *bufferInfo);
        static void asyncFormatChangedCallback(AMediaCodec *codec,
                                               void* userData,
                                               AMediaFormat *format);
        static void asyncErrorCallback(AMediaCodec *codec,
                                       void* userData,
                                       media_status_t error,
                                       int32_t actionCode,
                                       const char *detail);

    private:
        typedef std::function<void()>   IOTask;

    private:
        unsigned int                        mNumOutputBuffers   = 0;
        readSampleData_t                    mReadSampleDataFn;
        std::shared_ptr<AMediaCodec>        mMediaCodec;
        std::atomic<bool>                   mAtInputEOS;
        std::atomic<bool>                   mAtOutputEOS;

        pc_queue<IOTask>                    mIOQueue;
        std::thread                         mIOThread;
    };
}


#endif //MEDIATEST_SAMPLE_APP_H
