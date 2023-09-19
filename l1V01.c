
#include <stdio.h>              // Standard input/output (printf)
#include <stdlib.h>             // Standard library
#include <string.h>             // C string operations
#include <unistd.h>             // close device handle
#include <fcntl.h>              // open device handle
#include <errno.h>              // check errors
#include <sys/ioctl.h>          // icotl (read/write to driver)
#include <sys/stat.h>           // stat  (get file descriptor info)
#include <sys/mman.h>           // memory maps
#include <linux/videodev2.h>    // camera driver interface       
#include <math.h>            // math functions (sqrt, pow)
// imported libraries for image processing
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "./stb_image_write.h"
// Camera settings
#define CAM_WIDTH 640
#define CAM_HEIGHT 480
#define VIDEO_FILE_PATH "/dev/video0"
#define CAM_FORMAT V4L2_PIX_FMT_YUYV
// Process image
#define FILE_NAME "test.png"
#define CHANNEL_NUM 4
// Circle size
#define CIRCLE_WIDTH 5
#define CIRCLE_RADIUS 50
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))

typedef struct Pixel
    {
        unsigned char R;
        unsigned char G;
        unsigned char B;
        unsigned char A;
    } Pixel;


    static void YUYVtoRGB(unsigned char y, unsigned char u, unsigned char v, Pixel* _rgba)
    {
        int c = y - 16;
        int d = u - 128;
        int e = v - 128;
        _rgba->A = 255; //Alpha
        _rgba->R = MAX(0, MIN((298 * c + 409 * e + 128) >> 8, 255));
        _rgba->G = MAX(0, MIN((298 * c - 100 * d - 208 * e + 128) >> 8, 255));
        _rgba->B = MAX(0, MIN((298 * c + 516 * d + 128) >> 8, 255));
    }

void set_circle(Pixel* rgbConversion){
    int k = CAM_HEIGHT /2;
    int h = CAM_WIDTH /2;
    
    for(int y = 0; y < CAM_HEIGHT; y++){

       for (int x = 0; x < CAM_WIDTH; x++){
            //(x-h)² + (y-k)²= r²
            double r = sqrt(pow(x-h,2) + pow(y-k,2));
            if (r < CIRCLE_RADIUS && r > CIRCLE_RADIUS -CIRCLE_WIDTH){
                int px = (y)*(CAM_WIDTH) + (x);
                rgbConversion[px].R = 255;
                rgbConversion[px].G = 0;
                rgbConversion[px].B = 0;
                rgbConversion[px].A = 255;
            }

       } 
    }
    
}

int ProcessImage(const unsigned char *_yuv, int _size)
    {

        #define RGB_SIZE CAM_WIDTH * CAM_HEIGHT
        static Pixel rgbConversion[RGB_SIZE];
        int rgbIndex = 0;
        for (int i = 0; i < _size; i += 4)
        {
            unsigned char y1 = _yuv[i + 0];
            unsigned char u = _yuv[i + 1];
            unsigned char y2 = _yuv[i + 2];
            unsigned char v = _yuv[i + 3];
            YUYVtoRGB(y1, u, v, &rgbConversion[rgbIndex++]);
            YUYVtoRGB(y2, u, v, &rgbConversion[rgbIndex++]);
        }
        // rgbConversion now has the correct rgba data here (if you used V4L2_PIX_FMT_YUYV). Use the data to create a PNG image that is saved to disk.
    

        //save image to disk in png format

        
        set_circle(rgbConversion);
        stbi_write_png(FILE_NAME, CAM_WIDTH, CAM_HEIGHT, CHANNEL_NUM, rgbConversion, CAM_WIDTH*CHANNEL_NUM);


        return 0;

    }

int setup_camera(
    const int cam_width,
    const int cam_height,
    const int cam_format){
    // setting up camera settings
    int cameraHandle = open(VIDEO_FILE_PATH, O_RDWR, 0);
    struct v4l2_format format;
    memset(&format,0,sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = cam_width;
    format.fmt.pix.height = cam_height;
    format.fmt.pix.pixelformat = cam_format;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (ioctl(cameraHandle, VIDIOC_S_FMT, &format) < 0)
    {
        printf("VIDIOC_S_FMT Video format set fail\n");
        return -1;
    }
    printf("Camera Set up done!\n");
    return cameraHandle;
}



int main(){
    int cameraHandle = setup_camera(CAM_WIDTH, CAM_HEIGHT, CAM_FORMAT);
    if (cameraHandle < 0){
        return 1;
    }
    //Setting up buffer structure
        //requesting buffer
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cameraHandle, VIDIOC_REQBUFS, &req) < 0)
    {
        printf("VIDIOC_REQBUFS failed!\n");
        return -1;
    }
    // query the created buffers
    struct v4l2_buffer* buffers[2];
    //unsigned char* imageMemory[2]; // array to store memory mappings
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(cameraHandle, VIDIOC_QUERYBUF, &buf) < 0) {
            printf("VIDIOC_QUERYBUF failed!\n");
            return -1;
        }
        buffers[i] = (struct v4l2_buffer*)malloc(sizeof(struct v4l2_buffer)); // Allocate memory for buffer
        if (!buffers[i]) {
            printf("Failed to allocate memory for buffer %d\n", i);
            return -1;
        }
        *buffers[i] = buf; // Copy buffer information

        // Map the buffer to memory
        buffers[i]->m.userptr = 0; // Make sure the user pointer is cleared
        buffers[i]->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffers[i]->memory = V4L2_MEMORY_MMAP;
        if (ioctl(cameraHandle, VIDIOC_QBUF, buffers[i]) < 0) {
            printf("VIDIOC_QBUF failed!\n");
            return -1;
        }
    }


    // Image memory
    unsigned char** ImageMemory = (unsigned char**)malloc(sizeof(unsigned char*) * req.count);
    for (int i = 0; i < req.count; i++) {
        ImageMemory[i] = (unsigned char*)mmap(NULL, buffers[i]->length, PROT_READ, MAP_SHARED, cameraHandle, buffers[i]->m.offset);
        if (ImageMemory[i] == NULL) {
            printf("Image memory allocation failed!\n");
            return -1;
        }
    }



    //START STREAMING
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cameraHandle, VIDIOC_STREAMON, &type) < 0)
    {
        printf("VIDIOC_STREAMON failed!\n");
        return -1;

    }

    //CAPTURE IMAGE
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cameraHandle, VIDIOC_DQBUF, &buf) < 0)
    {
        return errno;
    }
    ProcessImage(ImageMemory[buf.index], buf.bytesused);
    printf("Buffer settings done!\n");
    if(ioctl(cameraHandle, VIDIOC_QBUF, &buf) < 0){
        printf("Queue failed\n");
        return 1;
    }    
    close(cameraHandle);
    free(ImageMemory);
    return 0;
}