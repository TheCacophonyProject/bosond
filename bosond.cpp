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


using namespace std::chrono;


const int width = 640;
const int height = 512;
const int pix_bytes = 2;
const int num_buffers = 2;

static std::string videoDevice("/dev/video");
static std::string socketPath;
static bool printTimings;
static bool sendFrames;


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

        TCLAP::SwitchArg sendArg("x", "no-send", "Just read frames without connecting to socket", true);
        cmd.add(sendArg);

        cmd.parse(argc, argv);

        videoDevice += std::to_string(deviceArg.getValue());
        socketPath = socketArg.getValue();
        printTimings = timingsArg.getValue();
        sendFrames = sendArg.getValue();

    } catch (TCLAP::ArgException &e) {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        exit(2);
    }
}


int main(int argc, char** argv) {
    int fd;
    struct v4l2_capability cap;

    processArgs(argc, argv);

    if ((fd = open(videoDevice.c_str(), O_RDWR)) < 0) {
        perror("Error : OPEN. Invalid Video Device\n");
        exit(1);
    }

    // Check VideoCapture mode is available
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("ERROR : VIDIOC_QUERYCAP. Video Capture is not available\n");
        exit(1);
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
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
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        perror("VIDIOC_S_FMT");
        exit(1);
    }

    // Allocate mmap buffers for retrieving video frames.
    struct v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufrequest.memory = V4L2_MEMORY_MMAP;
    bufrequest.count = num_buffers;
    if (ioctl(fd, VIDIOC_REQBUFS, &bufrequest) < 0) {
        perror("VIDIOC_REQBUFS");
        exit(1);
    }

    // Now find out about the buffers that were created and map them.
    struct v4l2_buffer bufferinfo[num_buffers];
    void *buffer[num_buffers];
    for (int i = 0; i < num_buffers; i++) {
        memset(&bufferinfo[i], 0, sizeof(struct v4l2_buffer));
        bufferinfo[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        bufferinfo[i].memory = V4L2_MEMORY_MMAP;
        bufferinfo[i].index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo[i]) < 0) {
            perror("VIDIOC_QUERYBUF");
            exit(1);
        }

        buffer[i] = mmap(NULL, bufferinfo[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bufferinfo[i].m.offset);
        if (buffer[i] == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
        memset(buffer[i], 0, bufferinfo[i].length);
    }

    // Init socket.
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sendFrames) {
        int send_size = bufferinfo[0].length;
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
        headers << "FrameSize: " << (width * height * pix_bytes) << '\n';
        headers << "PixelBits: " << (pix_bytes * 2) << '\n';
        headers << '\n';
        auto header_str = headers.str();
        if (sendall(sock, header_str.data(), header_str.length()) < 0) {
            perror("HEADERS");
            exit(1);
        }
    }

    // Activate streaming
    if (ioctl(fd, VIDIOC_STREAMON, &bufferinfo[0].type) < 0) {
        perror("VIDIOC_STREAMON");
        exit(1);
    }

    steady_clock::time_point t0 = steady_clock::now();

    // Indices of the buffer being filled (i_active) and the buffer
    // that is full and is ready to send (i_full).
    int i_active = 0;
    int i_full = 1;

    // Put the first buffer in the incoming queue.
    if (ioctl(fd, VIDIOC_QBUF, &bufferinfo[i_active]) < 0) {
        perror("VIDIOC_QBUF");
        exit(1);
    }

    int count = 0;
    for (;;) {
        // Wait for active buffer to be filled.
        if (ioctl(fd, VIDIOC_DQBUF, &bufferinfo[i_active]) < 0) {
            perror("VIDIOC_DQBUF");
            exit(1);
        }

        std::swap(i_active, i_full);  // Switch buffer roles

        // Put next buffer in the incoming queue so the video driver
        // can start filling it.
        if (ioctl(fd, VIDIOC_QBUF, &bufferinfo[i_active]) < 0) {
            perror("VIDIOC_QBUF");
            exit(1);
        }

        // Send full buffer while other buffer is being filled.
        if (sendFrames) {
            if (sendall(sock, (const char *) buffer[i_full], bufferinfo[i_full].length) < 0) {
                perror("SEND");
                exit(1);
            }
        }

        if (printTimings) {
            count++;
            if (count == 120) {
                steady_clock::time_point t1 = steady_clock::now();
                auto us = duration_cast<microseconds>(t1 - t0).count();
                auto rate = count / ((float)us / 1e6);
                std::cout << rate << "Hz" << std::endl;
                t0 = t1;
                count = 0;
            }
        }
    }

    return 0;
}
