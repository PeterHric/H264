#pragma once

#include <iostream>
#include <queue>
#include <atomic>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <chrono>

// Open/close video files
#include "opencv2/highgui/highgui.hpp"

using namespace std;
using namespace cv;

class AVideoStreamProcessor;

//const unsigned int wait_ms = 24;
const unsigned int wait_ms = 1;
const float nanosec_2_sec = .000'000'001; // Converts nano seconds to seconds: 5000'000'000 [ns] * nanosec_2_sec = 5s

/**
* @brief: Counts video processor statistics
*/
class AStatsCounter
{
public:
    virtual ~AStatsCounter() = default;

    // @brief: Calculates and prints post-process statistics to the std out
    virtual void printStatistics() = 0;

    // @brief: Called at the process begin to mark down time
    //         The motivation is effort to save cpu time
    virtual inline void startCount() = 0;

    // @brief: To be called every time a frame has been processed
    inline void frameTick()
    {
        ++_framesProcessed;
    }

protected:
    // @brief: Counter statistics
    unsigned long  _framesProcessed = 0;
    float _avgSpeed_fps = 0;
    float _processingTime_s = 0;

    // @brief: The timestamp of processing start
    time_t _startTime = 0;
};

/**
* @brief: Implementation of video processor statistics counter using ctime
*/
class StatsCounter : public AStatsCounter
{
public:
    virtual ~StatsCounter() = default;

    void printStatistics()
    {
        _processingTime_s = time(0) - _startTime;
        _avgSpeed_fps = _framesProcessed / _processingTime_s;

        cout << endl << "Processed frames: " << _framesProcessed << endl;
        cout << std::setprecision(2) << std::fixed;
        cout << endl << "Processing time: " << _processingTime_s << " [s]" << endl;
        cout << endl << "Average speed: " << _avgSpeed_fps << " [fps]" << endl;
    }

    inline void startCount()
    {
        if (_startTime == 0)
            _startTime = time(0);
    }
};

/**
* @brief: Implementation of video processor statistics counter std::chrono
*         Achieves better time precision
*/
class StatsCounterChrono : public AStatsCounter
{
public:
    virtual ~StatsCounterChrono() = default;

    void printStatistics()
    {
        auto nowTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        _processingTime_s = (nowTime - _startTime) * nanosec_2_sec;
        _avgSpeed_fps = _framesProcessed / _processingTime_s;

        cout << endl << "Processed frames: " << _framesProcessed << endl;
        std::cout << std::setprecision(2) << std::fixed;
        cout << endl << "Processing time: " << _processingTime_s << " [s]" << endl;
        cout << endl << "Average speed: " << _avgSpeed_fps << " [fps]" << endl;
    }

    inline void startCount()
    {
        if (_startTime == 0)
            _startTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }
};

/**
*  @brief: Interface for all sort of video workers
*/
class IStoppable
{
public:
    /**
    * @brief: Sets flag for thread immediate termination
    */
    virtual void setQuit(bool value) = 0;
};

/**
*  @brief: Base class for all sort of video workers
*/
class VideoStreamWorker : public IStoppable
{
public:
    /**
    * @brief: Sets flag for thread immediate termination
    * 
    * @param: [IN] value - true for worker loop quit
    */
    inline void setQuit(bool value) override
    {
        _quitLoop = value;
    }

protected:
    // @brief: flag for immediate thread quit
    atomic<bool> _quitLoop{ false };
};

/**
* @brief: Decodes the video stream and decomposes to individual frames
*/
class AVideoStreamDecoder : public VideoStreamWorker
{
public:
    virtual ~AVideoStreamDecoder() = default;

    /*
    * @breif:  Decodes the stream to the output buffer
    *
    * @param: [IN] source - the data source of video frames
    * @param: [IN/OUT] outBuffer - where to store the individual decoded frames
    * @param: [IN] whenDoneCallback - to be called to announce end of processing
    */         
    virtual void decode(VideoCapture& source, queue<Mat>& outBuffer, AVideoStreamProcessor& processor) = 0;
    //virtual void decode(VideoCapture& source, queue<Mat>& outBuffer, std::function<void()> whenDoneCallback) = 0;
};


class VideoStreamDecoder : public AVideoStreamDecoder
{
public:
    ~VideoStreamDecoder() = default;

    void decode(VideoCapture& source, queue<Mat>& outBuffer, AVideoStreamProcessor& processor) override;
    //void decode(VideoCapture& source, queue<Mat>& outBuffer, std::function<void()> whenDoneCallback) override;

    std::function<void()> decodingEndedCallback;
};

/**
* @brief: Applies defined operation on the decoded video stream
*/
class AVideoStreamProcessor : public VideoStreamWorker
{
public:
    virtual ~AVideoStreamProcessor() = default;
    
    /*
    * @breif:  Processes the video stream from the input buffer
    *
    * @param: [IN] winName - name of the playback window
    * @param: [IN] inBuffer - source video stream
    */
    virtual void process(const string& winName, queue<Mat>& inBuffer, AStatsCounter& counter) const = 0;

    /*
    * @breif: Applies the selected opeartion on the image
    *
    * @param: [IN/OUT] frame - the image frame to be applied with operation
    */
    virtual void applyOperation(Mat& frame) const = 0;

    /// @brief: Sets thread stop condition to given value
    inline void setDone(bool  value)
    {
        _decodingDone.store(value);
    }

    /// @brief: Gets thread stop condition
    inline bool getDone() { return _decodingDone.load(); }    

protected:
    // @brief: Signalises the thread function whether to continue or quit.
    atomic<bool> _decodingDone{ false };
};

/**
* @brief: Specialized implementation for our yellow filter
*/
class VideoStreamProcessor : public AVideoStreamProcessor
{
public:
    ~VideoStreamProcessor() = default;
    void process(const string& winName, queue<Mat>& inBuffer, AStatsCounter& counter) const override;

    // This is to protect reading buffer.empty() and storing to the same buffer in different threads
    //static std::mutex _buffMutex;

private:
    void applyOperation(Mat& frame) const override;
};

/**
* Organizes the orcehster of threads to maximize the performance
*/
class Orchestrator
{

public:
    explicit Orchestrator(VideoCapture& capture) :
        _videoSource(capture)
    {
    }

    inline void setStatsCounter(unique_ptr<AStatsCounter> counter)
    {
        _statsCounter = std::move(counter);
    }

    inline void setVideoDecoder(unique_ptr<AVideoStreamDecoder> decoder)
    {
        _videoDecoder = std::move(decoder);
    }

    inline void setVideoProcessor(unique_ptr<AVideoStreamProcessor> processor)
    {
        _videoProcessor = std::move(processor);
    }

    bool run();

private:
    /// @brief: The video decoding object
    std::unique_ptr<AVideoStreamDecoder> _videoDecoder;
    
    /// @brief: The video processing object
    std::unique_ptr<AVideoStreamProcessor> _videoProcessor;

    /// @brief: Counts statistics during video processing
    std::unique_ptr<AStatsCounter> _statsCounter;

    /// @brief: Producer - consumer lock mechanism
    //std::counting_semaphore<1> _semaphore;

    /// @brief: The buffer of frames decoded from src file, stored here -> to be processed and displayed
    std::queue<cv::Mat> _buffer;
    // Or use MS concurrent_queue
    // https://docs.microsoft.com/en-us/cpp/parallel/concrt/reference/concurrent-queue-class?view=vs-2019

    // Or use BOOST lock-free structure: https://www.boost.org/doc/libs/1_53_0/doc/html/lockfree.html
    //boost::lockfree::spsc_queue<cv::Mat> _cBuffer;  -> i.e. circullar buffer

    /// @brief: The video to be processed and displayed
    VideoCapture& _videoSource;
};
