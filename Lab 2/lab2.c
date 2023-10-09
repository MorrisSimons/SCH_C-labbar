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
#include <math.h>               // math functions (sqrt, pow)
#include <SDL2/SDL.h>           // Image rendering
#include <omp.h>                // Used in multithreading
#include <pthread.h>            // Pthread used in image capture
#include <stdbool.h>            // Used is SDL_Event
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
#define IMG_THREADS 8
// Circle size
#define CIRCLE_WIDTH 5
#define CIRCLE_RADIUS 50
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))
// Var for SDL window
SDL_Window* g_window = NULL;
SDL_Renderer* g_renderer = NULL;
SDL_Texture* g_streamTexture = NULL;
// Declare global variables for pos_x and pos_y
int pos_x = -1;
int pos_y = -1;
int last_dic = -1;
int brightest_red = 0;
// Used in timestamping
int loops = 0;
int timevalue = 0;
// thread flag to capture img
int captureflag = 0;
bool exitprogramflag = false;
typedef struct Pixel
    {
        unsigned char B;
        unsigned char G;
        unsigned char R;
        unsigned char A;
    } Pixel; //Used to store pixel colors

typedef struct 
    {
        int start;
        int end;
        int rgb_index;
    } ImageParts; // Used in multithreading when splitting image

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


void find_laser(Pixel* rgbConversion)
{
    pos_x = -1;
    pos_y = -1;
    for (int y = 0; y < CAM_HEIGHT; y++)
    {
        for (int x = 0; x < CAM_WIDTH; x++)
        {
            int px = (y) * (CAM_WIDTH) + (x);
            if (
                rgbConversion[px].R >= 210 &&
                rgbConversion[px].A >= 200 &&
                rgbConversion[px].G <= 60 &&
                rgbConversion[px].B <= 60 &&
                rgbConversion[px].R >= rgbConversion[brightest_red].R && 
                rgbConversion[px].A >= rgbConversion[brightest_red].A
                )
            {
                brightest_red = px;
                pos_x = x;
                pos_y = y;
            }
        }
    }
}

void direction(int px_widht, int px_height){
    if (px_height >= ((CAM_HEIGHT * 3) / 4) && (last_dic != 0))
    {
        // BACK
        last_dic = 0;
    }
    else if (px_widht <= (CAM_WIDTH / 4) && px_height <= ((CAM_HEIGHT * 3) / 4) && last_dic != 1)
    {
        // LEFT
        last_dic = 1;
    }
    else if (px_widht >= (CAM_WIDTH * 3) / 4 && px_height <= ((CAM_HEIGHT * 3) / 4) && last_dic != 2)
    {
        // RIGHT
        last_dic = 2;
    }
    else if (px_widht <= ((CAM_WIDTH * 3) / 4) && (px_widht) >= (CAM_WIDTH / 4) && px_height <= ((CAM_HEIGHT * 3) / 4) && last_dic != 3)
    {
        // FORWARD
        last_dic = 3;
    }
}

void set_circle(Pixel* rgbConversion)
{
    int k = CAM_HEIGHT / 2;
    int h = CAM_WIDTH / 2;
    // Call find_laser to get h and k
    find_laser(rgbConversion);
    
    if (pos_x == -1 || pos_y == -1)
        return;
    direction(pos_x, pos_y);
    for (int y = 0; y < CAM_HEIGHT; y++)
    {
        for (int x = 0; x < CAM_WIDTH; x++)
        {
            double r = sqrt(pow(x - pos_x, 2) + pow(y - pos_y, 2));
            if (r < CIRCLE_RADIUS && r > CIRCLE_RADIUS - CIRCLE_WIDTH)
            {
                int px = (y) * (CAM_WIDTH) + (x);
                rgbConversion[px].R = 0;
                rgbConversion[px].G = 0;
                rgbConversion[px].B = 255;
                rgbConversion[px].A = 255;
            }
        }
    }
}

double CalculateTime(double t0){
    loops++;
    double threading = (omp_get_wtime() - t0) * 1000;
    double timevalue = timevalue + threading;
    double avg_time = timevalue / (double)loops;
    return avg_time;
}

void PrintImageData(double avg_time, int last_dic){
    switch (last_dic)
    {
    case 0:
        printf("Thread Time avg: %f ms and direction back       \n",avg_time); 
        break;
    case 1:
        printf("Thread Time avg: %f ms and direction left       \n",avg_time); 
        break;
    case 2:
        printf("Thread Time avg: %f ms and direction right      \n",avg_time); 
        break;
    case 3:
        printf("Thread Time avg: %f ms and direction forward    \n",avg_time); 
        break;
    default:
        printf("Thread Time avg: %f ms and direction not deteced\n",avg_time); 
    }
}

void DisplayImg(){
    SDL_UnlockTexture(g_streamTexture); //lab inst
    SDL_RenderCopy(g_renderer, g_streamTexture, NULL, NULL); // Kommer från lab ins
    SDL_RenderPresent(g_renderer);// lab inst
}

double ProcessImage(const unsigned char *_yuv, int _size, Pixel* rgbConversion){
    //printf("Processing image! \n");
    //Multi threading setting
    int Threads = IMG_THREADS;
    ImageParts parts[Threads]; //array to store parts using our local struct
    //split image into parts
    int part_size = _size / Threads;
    for (int i= 0; i < Threads; i++){
        parts[i].start = part_size * i;
        parts[i].end = (i + 1) * part_size -1;
        parts[i].rgb_index = part_size * i / 2;
    }
    //printf("Starting multithread of image\n");
    double t0 = omp_get_wtime();
    #pragma omp parallel num_threads(Threads)
    {
        //Get ID of the current thread:
        int id = omp_get_thread_num();
        ImageParts part = parts[id];
        //printf("This is the part from %d [start:%d, end:%d and rgbindex: %d]\n",id, part.start, part.end, part.rgb_index);
        for (int i = part.start; i < part.end; i += 4)
        {
            unsigned char y1 = _yuv[i + 0];
            unsigned char u = _yuv[i + 1];
            unsigned char y2 = _yuv[i + 2];
            unsigned char v = _yuv[i + 3];
            YUYVtoRGB(y1, u, v, &rgbConversion[part.rgb_index++]);
            YUYVtoRGB(y2, u, v, &rgbConversion[part.rgb_index++]);
        }
    }
    //printf("im working \n");
    set_circle(rgbConversion);
    return t0;
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
        return 1;
    }
    printf("Camera Set up done!\n");
    return cameraHandle;
}

int setup_req_buffer(
    const int cameraHandle){
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
        return 1;
    }
    return req.count;
}

int setup_SDL(){
    //init SDL
    SDL_Init(SDL_INIT_VIDEO);
    //Create window
    g_window = SDL_CreateWindow("SDL Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, CAM_WIDTH, CAM_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (g_window == NULL){
        printf("G-window error \n");
        return 1;
    }
    
    // Create render window
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (g_renderer == NULL){
        printf("G-renderer error \n");
        return 1;
    }
    // Create a texture we can stream to
    g_streamTexture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CAM_WIDTH, CAM_HEIGHT);
    if (g_streamTexture == NULL){
        printf("G-streamTexture error \n");
        return 1;
    }
}

void CaptureImage(Pixel* rgbConversion){
    while(!exitprogramflag){
        Pixel* capture_pixels = malloc(CAM_WIDTH * CAM_HEIGHT * sizeof(Pixel));
        memcpy(capture_pixels, rgbConversion, CAM_WIDTH * CAM_HEIGHT * sizeof(Pixel));
        for(int i = 0; i < CAM_HEIGHT * CAM_WIDTH; i++){
            int r = capture_pixels[i].B;
            int b = capture_pixels[i].R;
            capture_pixels[i].R = r;
            capture_pixels[i].B = b;
        }
        printf("Photo saved as test.png\n");
        //Fix me, do a return on rgb conversion
        stbi_write_png(FILE_NAME, CAM_WIDTH, CAM_HEIGHT, CHANNEL_NUM, capture_pixels, CAM_WIDTH * CHANNEL_NUM);
        captureflag = 0;
        free(capture_pixels);
    }
    
}

int main(){
    int cameraHandle = setup_camera(CAM_WIDTH, CAM_HEIGHT, CAM_FORMAT);
    if (cameraHandle < 0){
        return 1;
    }
    int request_buffers_count = setup_req_buffer(cameraHandle);
    if (cameraHandle < 0){
        return 1;
    }
    // query the created buffers
    struct v4l2_buffer* buffers[2];
    for (int i = 0; i < request_buffers_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(cameraHandle, VIDIOC_QUERYBUF, &buf) < 0) {
            printf("VIDIOC_QUERYBUF failed!\n");
            return 1;
        }
        buffers[i] = (struct v4l2_buffer*)malloc(sizeof(struct v4l2_buffer)); // Allocate memory for buffer
        if (!buffers[i]) {
            printf("Failed to allocate memory for buffer %d\n", i);
            return 1;
        }
        *buffers[i] = buf; // Copy buffer information

        // Map the buffer to memory
        buffers[i]->m.userptr = 0; // Make sure the user pointer is cleared
        buffers[i]->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffers[i]->memory = V4L2_MEMORY_MMAP;
        if (ioctl(cameraHandle, VIDIOC_QBUF, buffers[i]) < 0) {
            printf("VIDIOC_QBUF failed!\n");
            return 1;
        }
        }
        // Image memory
        unsigned char** ImageMemory = (unsigned char**)malloc(sizeof(unsigned char*) * request_buffers_count);
        for (int i = 0; i < request_buffers_count; i++) {
            ImageMemory[i] = (unsigned char*)mmap(NULL, buffers[i]->length, PROT_READ, MAP_SHARED, cameraHandle, buffers[i]->m.offset); // Fix me free mmap
            if (ImageMemory[i] == NULL) {
                printf("Image memory allocation failed!\n");
                return 1;
            }
        }
        // Setup for streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(cameraHandle, VIDIOC_STREAMON, &type) < 0)
        {
            printf("VIDIOC_STREAMON failed!\n");
            return 1;

        }
        // Setup SDL
        int SDL_state = setup_SDL();
        if (SDL_state < 0){
            return 1;
        }
        void *pixels;
        int pitch;
        SDL_LockTexture(g_streamTexture, NULL, &pixels, &pitch);
        Pixel* rgbConversion = (Pixel*)pixels;
        bool quit = false;
        SDL_Event event;
        int buffer_index = 0; //Index for what buffer we are useing
        // START STREAMING
        pthread_t CaptureThread;
        pthread_create(&CaptureThread, NULL, (void *(*)(void *)) CaptureImage, rgbConversion);
        while(!quit){
            //CAPTURE IMAGE
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (ioctl(cameraHandle, VIDIOC_DQBUF, &buf) < 0)
            {
                return errno;
            }
            double t0 = ProcessImage(ImageMemory[buffer_index], buf.bytesused, rgbConversion);
            DisplayImg();
            double avg_time = CalculateTime(t0);
            PrintImageData(avg_time, last_dic);
            if(ioctl(cameraHandle, VIDIOC_QBUF, &buf) < 0){
                printf("Queue failed\n");
                return 1;
                }
            buffer_index = buffer_index++ % request_buffers_count; //rotate buffer function
        while(SDL_PollEvent(&event)){
                //printf("knapp down\n");
                if (event.type == SDL_KEYDOWN)
                {
                        if (event.key.keysym.sym == SDLK_ESCAPE){
                            quit = true;
                            return 0;
                        }
                        if (event.key.keysym.sym == SDLK_c){
                            captureflag = 1;

                        }
                }
            }
        }
        // Free up used space 
        for (int i = 0; i < request_buffers_count; i++) {
            if (munmap(ImageMemory[i], buffers[i]->length) < 0) {
                perror("munmap failed");
                return 1;
            }
        }
        for (int i = 0; i < request_buffers_count; i++) {
            free(buffers[i]);
            }
        free(ImageMemory);
        printf("closeing window \n");
        //closeing SDL window down
        exitprogramflag = true;
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        close(cameraHandle);
        

}