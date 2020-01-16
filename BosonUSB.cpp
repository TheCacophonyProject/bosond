#include <stdio.h>
#include <fcntl.h>               // open, O_RDWR
#include <opencv2/opencv.hpp>
#include <unistd.h>              // close
#include <sys/ioctl.h>           // ioctl
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/videodev2.h>


using namespace cv;

void inspect(Mat input_16, int height, int width) {
    int i, j;  // aux variables

    // auxiliary variables for AGC calcultion
    unsigned int max1=0;         // 16 bits
    unsigned int min1=0xFFFF;    // 16 bits
    unsigned int value1, value2, value3;

    // RUN a super basic AGC
    for (i=0; i<height; i++) {
        for (j=0; j<width; j++) {
            if (i==0 && j==0) {
                unsigned int x = input_16.at<unsigned short>(i, j);
                printf("x=%d\n", x);
                unsigned int y = ((x & 0xff00) >> 8) + ((x & 0xff) << 8);
                printf("y=%d\n", y);
            }

            value1 =  input_16.at<uchar>(i,j*2+1) & 0XFF ;  // High Byte
            value2 =  input_16.at<uchar>(i,j*2) & 0xFF  ;    // Low Byte
            value3 = ( value1 << 8) + value2;
            if ( value3 <= min1 ) {
                min1 = value3;
            }
            if ( value3 >= max1 ) {
                max1 = value3;
            }
        }
    }

    printf("min=%d max=%d\n", min1, max1);
}


int main(int argc, char** argv)
{
    int fd;
    struct v4l2_capability cap;
    char video[20];   // To store Video Port Device
    char label[50];   // To display the information
    char thermal_sensor_name[20];  // To store the sensor name
    int width;
    int height;

    // To record images
    std::vector<int> compression_params;
    compression_params.push_back(IMWRITE_PXM_BINARY);

    // Video device by default
    sprintf(video, "/dev/video1");
    sprintf(thermal_sensor_name, "Boson_640");
    width=640;
    height=512;

    if((fd = open(video, O_RDWR)) < 0){
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

    if(ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo) < 0){
        perror("VIDIOC_QUERYBUF");
        exit(1);
    }

    // map fd+offset into a process location (kernel will decide due to our NULL). lenght and
    // properties are also passed
    printf(">>> Image width   = %i\n", width);
    printf(">>> Image height  = %i\n", height);
    printf(">>> Buffer length = %i\n", bufferinfo.length);

    void * buffer_start = mmap(NULL, bufferinfo.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bufferinfo.m.offset);
    if (buffer_start == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Fill this buffer with ceros. Initialization. Optional but nice to do
    memset(buffer_start, 0, bufferinfo.length);

    // Activate streaming
    int type = bufferinfo.type;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0){
        perror("VIDIOC_STREAMON");
        exit(1);
    }


    // Declarations for RAW16 representation
    // Will be used in case we are reading RAW16 format
    // Boson320 , Boson 640
    Mat thermal16(height, width, CV_16U, buffer_start);   // OpenCV input buffer  : Asking for all info: two bytes per pixel (RAW16)  RAW16 mode`

    // Declarations for Zoom representation
    // Will be used or not depending on program arguments
    Size size(640,512);

    // Reaad frame, do AGC, paint frame
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

        // cv::Scalar v = cv::mean(thermal16);
        // printf("%.0f\n", v.val[0]);

        inspect(thermal16, height, width);

        if (waitKey(1) == 'q') {
            printf(">>> 'q' key pressed. Quitting !\n");
            break;
        }
    }
    // Finish Loop . Exiting.

    // Deactivate streaming
    if( ioctl(fd, VIDIOC_STREAMOFF, &type) < 0 ){
        perror("VIDIOC_STREAMOFF");
        exit(1);
    };

    close(fd);
    return EXIT_SUCCESS;
}
