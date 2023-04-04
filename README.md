###### H264 #####

This is a simple demo of Video files processing with OpenCV with C++

------------- Windows:

You will need to install OpenCV and copy these .dll files to the folder, where you run your .exe file:

*opencv_world440.dll*

*opencv_videoio_ffmpeg440_64.dll*


To build .exe from source and the .sln use Visual Studio:

Run in the release directory:

*H264.exe file_name.mp4*

or

*H264.exe -h  for help*

If you do not enter file name, the program will use first available web camera as video source


-------------- Linux Ubuntu/Debian :
sudo apt update

 * Install c++ build essentials:
sudo apt install build-essential

 * Install OpenCV:
sudo apt install libopencv-dev

 * Install OpenCV with CUDA support and CUDA toolkit:
sudo apt install nvidia-cuda-toolkit
sudo apt install libopencv-cuda-dev

Build:
g++ H264.cpp -o H264 -I /usr/include/opencv4/pencv2 -L /usr/local/lib -lopencv

Run:
*H264 file_name.mp4*

If you do not enter file name, the program will use first available web camera as video source