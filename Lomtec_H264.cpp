// Lomtec_H264.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <thread>
#include <functional>
#include "Lomtec_H264.h"
#include "opencv2/imgproc/imgproc.hpp"

//std::mutex Orchestrator::_buffMutex; // This would not compile ...
std::mutex buffMutex; // This is to protect reading buffer.empty() and storing to the same buffer in different threads

int main(int argc, char* filename[]) {        
    try
    {
        cv::CommandLineParser parser(argc, filename, "{help h||}{@input||}");
        if (parser.has("help"))
        {
            std::cout << "Expecting 1 mandatory argument: path to an mp4 file ! Exiting..." << endl;
            return -1;
        }
        std::string arg = parser.get<std::string>("@input");
        if (arg.empty())
        {
            std::cout << "The (only) parameter must be a VALID path to an mp4 file ! Exiting..." << endl;
            return -1;
        }

        VideoCapture capture(arg, cv::CAP_FFMPEG);  // cv::CAP_ANY | cv::CAP_FFMPEG         

        if (!capture.isOpened()) //if this fails, try to open as a video camera, through the use of an integer param
        {
            auto name = arg.c_str();
            std::cout << "The file: " << name << " could not be open. Incompatible file format, or corrupt data ? Trying to open PC's web-cam.." << endl;
            capture.open(atoi(name));
        }

        if (!capture.isOpened())
        {
            cout << "Opening web cam as a source failed ! Exiting ..." << endl;
            return -1;
        }

        Orchestrator orcherstrator(capture);
        //orcherstrator.setStatsCounter(make_unique<StatsCounter>());  // Choose one of possible implementations
        orcherstrator.setStatsCounter(make_unique<StatsCounterChrono>());
        orcherstrator.setVideoDecoder(make_unique<VideoStreamDecoder>());
        orcherstrator.setVideoProcessor(make_unique<VideoStreamProcessor>());
        return orcherstrator.run();
        

        return true;
    }
    catch (std::exception& e)
    {
        cout << "Error encountered !  Description: " << e.what() << endl;
    }
    catch (...)
    {
        cout << "Unknown exception caught.." << endl;
    }
}

void VideoStreamDecoder::decode(VideoCapture& source, queue<Mat>& outBuffer, AVideoStreamProcessor& processor)
{
    thread feedWorker([&]() 
    {
        string window_name = "Original video";
        //namedWindow(window_name, WINDOW_KEEPRATIO);
        Mat frame;
        unsigned long frameCnt = 0;        
        auto frameNum = source.get(VideoCaptureProperties::CAP_PROP_FRAME_COUNT);

        while (frameCnt < frameNum)
        {
            source >> frame;
            ++frameCnt;

            if (_quitLoop.load())
            {
                break; // immediate quit
            }

            {
                //const std::lock_guard<std::mutex> lock(Orchestrator::_buffMutex);
                const std::lock_guard<std::mutex> lock(buffMutex);

                /*
                if (!frame.empty())
                    imshow(window_name, frame);

                waitKey(wait_ms); // [ms]
                */

                // Store the decoded image to the buffer
                outBuffer.push(frame);
            }            
        }

        // Yeeey, we are done reading !
        processor.setDone(true); // Let the world know !
        cout << "Decoded and passed: " << frameCnt << " frames for processing" << endl;
    });

    feedWorker.detach();
}

void VideoStreamProcessor::process(const string& winName, queue<Mat>& inBuffer, AStatsCounter& counter) const
{    
    thread processWorker([&]() // Or use std::async
    {
        namedWindow(winName, WINDOW_AUTOSIZE); //resizable window; WINDOW_KEEPRATIO | WINDOW_AUTOSIZE | WINDOW_FULLSCREEN
        Mat frame;

        while (true)
        {
            // Make sure that we have something to process
            bool empty = false;
            
            { // ToDo: Is this thread safe sync. really needed ?
                const std::lock_guard<std::mutex> lock(buffMutex); //lock(Orchestrator::_buffMutex);
                empty = inBuffer.empty();
            }  

            if (_quitLoop.load())
            {
                break; // immediate quit
            }

            if (empty)
            {
                if (_decodingDone.load())
                {
                    break; // Exit on DONE trigger
                }

                waitKey(1); // [ms] -> seems quicker than std::this_thread::sleep_for(std::chrono::milliseconds(1))
                continue;
            }

            frame = inBuffer.front();
            // Process the frame 
            // Yes, the late binding (and even exra funtion call) here goes against the requirement:
            // "Achieve MAX of Processing Throughput"
            // But another requirement tells to do it in C++, not 'C' with a C++ Compiler :)
            
            //imshow("Origo", frame);
            applyOperation(frame);
            counter.frameTick(); // Increment 1 frame processed

            imshow(winName, frame);
            waitKey(wait_ms); // [ms]            

            inBuffer.pop();            
        }

        counter.printStatistics();
    });

    processWorker.detach();
}


void VideoStreamProcessor::applyOperation(Mat& frame) const
{
    // Convert the image into an HSV image
    cv::cvtColor(frame, frame, cv::COLOR_BGR2HSV);

    // Replace the yellow color(s) with black
    Mat mask;
    //cv::inRange(frame, cv::Scalar(60,0,0), cv::Scalar(60,255,255), mask);
    //cv::inRange(frame, cv::Scalar(61, 100, 100), cv::Scalar(120, 255, 255), mask); // Glare & glow
    //cv::inRange(frame, cv::Scalar(61, 30, 50), cv::Scalar(80, 255, 255), mask);  // Green
    cv::inRange(frame, cv::Scalar(20, 30, 50), cv::Scalar(50, 255, 255), mask);  // Yelow ?
    //imshow("mask", mask);
    //waitKey(wait_ms);

    cv::bitwise_not(frame, frame, mask);
    cv::cvtColor(frame, frame, cv::COLOR_HSV2BGR);
}

bool Orchestrator::run()
{    
    if (nullptr == _statsCounter.get())
    {
        cout << "Error: Statistic counter has not been set ! Exitting..." << endl;
        return false;
    }

    if (nullptr == _videoDecoder.get())
    {
        cout << "Error: Video decoder has not been set ! Exiting..." << endl;
        return false;
    }
    
    if (nullptr == _videoProcessor.get())
    {
        cout << "Error: Video processor has not been set ! Exiting..." << endl;
        return false;
    }

    try
    {
        _statsCounter->startCount();

        // Feeds the work buffer in a separate thread
        _videoDecoder->decode(_videoSource,
            _buffer,
            *_videoProcessor);

        string windowTitle = "Processed video playback";
        // Processes the frames from the buffer in a separate thead
        _videoProcessor->process(windowTitle,
            _buffer,
            *_statsCounter);
        
        while (true)
        {             
            auto key = getchar();
            //waitKey(100);
            cout << "Enter: p,P - print | Enter: q,Q - Exit" << endl;

            switch (key) {
            case 'q':
            case 'Q':
            {
                return 0;
                break;
            }
            case 'p':
            case 'P':
            {
                _statsCounter->printStatistics();
                break;
            }
            default:
                break;
            }
        }
        
    }
    catch (Exception& ex)
    {
        cout << "Open CV exception ! Error when processing the video: " << ex.what() << endl;
    }
    catch (std::exception& ex)
    {
        cout << "Error when processing the video: " << ex.what() << endl;
    }
    catch (...)
    {
        cout << "Unspecified error when processing the video." << endl;
    }

    _videoDecoder->setQuit(true);
    _videoProcessor->setQuit(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // wait for threads to exit correctly

    return true;
}
