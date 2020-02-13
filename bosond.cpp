#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>               // open, O_RDWR
#include <unistd.h>              // close
#include <sys/ioctl.h>           // ioctl
#include <sys/mman.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <linux/videodev2.h>
#include <string>
#include <iostream>
#include <chrono>
#include <tclap/CmdLine.h>

using std::chrono::system_clock;

const int width = 640;
const int height = 512;

static std::string videoDevice("/dev/video");
static std::string socketPath;
static bool printTimings;

int sendall(int sock, const char *data, size_t len) {
    int left = len;
    int n;

    while (left > 0) {
        n = send(sock, data, left, 0);
        if (n < 0) {
            return n;
        }
        left -= n;
        data += n;
    }
    return 0;
}


void processArgs(int argc, char **argv) {
    try {

        TCLAP::CmdLine cmd("Read FLIR Boson frames", ' ', "0.1");

        TCLAP::ValueArg<int> deviceArg("d", "device", "Video device number to use", false, 0, "int");
        cmd.add(deviceArg);

        TCLAP::ValueArg<std::string> socketArg("p", "socket-path", "Path to output socket", false, "/var/run/lepton-frames", "string");
        cmd.add(socketArg);

        TCLAP::SwitchArg timingsArg("t", "print-timing", "Print frame timings");
        cmd.add(timingsArg);

        cmd.parse(argc, argv);

        videoDevice += std::to_string(deviceArg.getValue());
        socketPath = socketArg.getValue();
        printTimings = timingsArg.getValue();

    } catch (TCLAP::ArgException &e) {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        exit(2);
    }
}


int main(int argc, char** argv) {
    int fd;
    struct v4l2_capability cap;

    processArgs(argc, argv);

    if((fd = open(videoDevice.c_str(), O_RDWR)) < 0){
        perror("Error : OPEN. Invalid Video Device\n");
        exit(1);
    }

    // Check VideoCapture mode is available
    if(ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0){
        perror("ERROR : VIDIOC_QUERYCAP. Video Capture is not available\n");
        exit(1);
    }

    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
        fprintf(stderr, "The device does not handle single-planar video capture.\n");
        exit(1);
    }

    struct v4l2_format format;

    // Common varibles
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_Y16;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;

    // request desired FORMAT
    if(ioctl(fd, VIDIOC_S_FMT, &format) < 0){
        perror("VIDIOC_S_FMT");
        exit(1);
    }

    // we need to inform the device about buffers to use.
    // and we need to allocate them.
    // we’ll use a single buffer, and map our memory using mmap.
    // All this information is sent using the VIDIOC_REQBUFS call and a
    // v4l2_requestbuffers structure:
    struct v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest.memory = V4L2_MEMORY_MMAP;
    bufrequest.count = 1;   // we are asking for one buffer

    if(ioctl(fd, VIDIOC_REQBUFS, &bufrequest) < 0){
        perror("VIDIOC_REQBUFS");
        exit(1);
    }

    // Now that the device knows how to provide its data,
    // we need to ask it about the amount of memory it needs,
    // and allocate it. This information is retrieved using the VIDIOC_QUERYBUF call,
    // and its v4l2_buffer structure.
    struct v4l2_buffer bufferinfo;
    memset(&bufferinfo, 0, sizeof(bufferinfo));

    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufferinfo.memory = V4L2_MEMORY_MMAP;
    bufferinfo.index = 0;

    if (ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo) < 0) {
        perror("VIDIOC_QUERYBUF");
        exit(1);
    }

    void *buffer_start = mmap(NULL, bufferinfo.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bufferinfo.m.offset);
    if (buffer_start == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    memset(buffer_start, 0, bufferinfo.length);

    // Init socket.
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);

    int send_size = bufferinfo.length;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const void *)&send_size, sizeof(send_size)) < 0) {
        perror("SETSOCKOPT");
        exit(1);
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path)-1);

    if (connect(sock, (sockaddr*) (&addr), sizeof(addr)) < 0) {
        perror("CONNECT");
        exit(1);
    }

    std::ostringstream headers;
    headers << "Brand: flir\n";
    headers << "Model: boson\n";
    headers << "ResX: " << width << '\n';
    headers << "ResY: " << height << '\n';
    headers << "FPS: 60\n";
    headers << "FrameSize: " << (width * height * 2) << '\n';
    headers << "PixelBits: 16\n";
    headers << '\n';
    auto header_str = headers.str();
    if (sendall(sock, header_str.data(), header_str.length()) < 0) {
        perror("HEADERS");
        exit(1);
    }

    // Activate streaming
    int type = bufferinfo.type;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0){
        perror("VIDIOC_STREAMON");
        exit(1);
    }

    system_clock::time_point t0 = system_clock::now();
    int count = 0;
    for (;;) {
        // Put the buffer in the incoming queue.
        if(ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0){
            perror("VIDIOC_QBUF");
            exit(1);
        }

        // The buffer's waiting in the outgoing queue.
        if(ioctl(fd, VIDIOC_DQBUF, &bufferinfo) < 0) {
            perror("VIDIOC_QBUF");
            exit(1);
        }

        if (printTimings) {
            count++;
            if (count == 100) {
                system_clock::time_point t1 = system_clock::now();
                std::cout << "td = " << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() << "[µs]" << std::endl;
                t0 = t1;
                count = 0;
            }
        }

        if (sendall(sock, (const char *) buffer_start, bufferinfo.length) < 0) {
            perror("SEND");
            exit(1);
        }
    }

    // Deactivate streaming
    if( ioctl(fd, VIDIOC_STREAMOFF, &type) < 0 ){
        perror("VIDIOC_STREAMOFF");
        exit(1);
    };

    close(fd);
    return 0;
}
