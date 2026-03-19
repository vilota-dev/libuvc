#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "libuvc/libuvc.h"
#include <sys/time.h>  // for gettimeofday
#include <stdatomic.h>

static int v4l2_fd = -1;

static uint8_t *yuyv_buf = NULL;

volatile sig_atomic_t stop_flag = 0;
static uvc_frame_t *rgb = NULL;

void sigint_handler(int sig) {
    stop_flag = 1;  // just set flag
}
// ---- RGB24 → YUYV422 CONVERSION ----
void rgb_to_yuyv(uint8_t *rgb, uint8_t *yuyv, int width, int height)
{
    for (int i = 0, j = 0; i < width * height * 3; i += 6, j += 4) {
        int r1 = rgb[i],   g1 = rgb[i+1], b1 = rgb[i+2];
        int r2 = rgb[i+3], g2 = rgb[i+4], b2 = rgb[i+5];

        int y1 = ((66*r1 + 129*g1 + 25*b1 + 128) >> 8) + 16;
        int y2 = ((66*r2 + 129*g2 + 25*b2 + 128) >> 8) + 16;

        int u  = ((-38*r1 - 74*g1 + 112*b1 + 128) >> 8) + 128;
        int v  = ((112*r1 - 94*g1 - 18*b1 + 128) >> 8) + 128;

        if (y1 > 235) y1 = 235; if (y1 < 16) y1 = 16;
        if (y2 > 235) y2 = 235; if (y2 < 16) y2 = 16;
        if (u > 240) u = 240; if (u < 16) u = 16;
        if (v > 240) v = 240; if (v < 16) v = 16;

        yuyv[j+0] = y1;
        yuyv[j+1] = u;
        yuyv[j+2] = y2;
        yuyv[j+3] = v;
    }
}

// ---- UVC CALLBACK ----
void cb(uvc_frame_t *frame, void *ptr) {

  if(stop_flag ==0){
  if (!frame || frame->data_bytes == 0)
        return;

  
    
    static struct timeval last_cb_time = {0};  // store last callback time

    int width = frame->width;
    int height = frame->height;

    // ---- TIME BETWEEN CALLBACKS ----
    struct timeval now;
    gettimeofday(&now, NULL);
    if (last_cb_time.tv_sec != 0) {
        double interval_ms = (now.tv_sec - last_cb_time.tv_sec) * 1000.0 +
                             (now.tv_usec - last_cb_time.tv_usec) / 1000.0;
        printf("Callback interval: %.2f ms\n", interval_ms);
        if (interval_ms < 17.0) {  // 25 FPS → 1000/25 = 40 ms
            return; // skip this frame
        }
    }
    last_cb_time = now;

    // ---- ALLOCATE BUFFERS ----
    if (!rgb)
        rgb = uvc_allocate_frame(width * height * 3);

    if (!yuyv_buf)
        yuyv_buf = malloc(width * height * 2);

    if (!rgb || !yuyv_buf)
        return;

    // ---- DECODE MJPEG → RGB ----
    uvc_mjpeg2rgb(frame, rgb);

    // ---- CONVERT RGB → YUYV ----
    rgb_to_yuyv(rgb->data, yuyv_buf, width, height);

    // ---- WRITE TO V4L2 LOOPBACK ----
    ssize_t ret = write(v4l2_fd, yuyv_buf, width * height * 2);
    if (ret < 0)
        perror("write");
  }
}


// ---- MAIN ----
int main() {
    uvc_context_t *ctx;
    uvc_device_t *dev;
    uvc_stream_ctrl_t ctrl;
     uvc_device_handle_t *devh = NULL;

    signal(SIGINT, sigint_handler);

    // ---- OPEN V4L2 LOOPBACK ----
    v4l2_fd = open("/dev/video10", O_WRONLY);
    if (v4l2_fd < 0) {
        perror("open /dev/video10");
        return -1;
    }
    
    // Set format: YUYV422
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = 1920;
    fmt.fmt.pix.height = 1080;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.bytesperline = 1920 * 2;
    fmt.fmt.pix.sizeimage = 1920 * 1080 * 2;

    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }
   
    printf("V4L2 loopback configured (YUYV422)\n");

    // ---- INIT UVC ----
    uvc_init(&ctx, NULL);
    if (uvc_find_device(ctx, &dev, 16981, 24065, NULL) < 0) {
        printf("Device not found\n");
        return -1;
    }

    if (uvc_open(dev, &devh) < 0) {
        printf("Failed to open device\n");
        return -1;
    }

    // ---- STREAM CONFIG ----
    if (uvc_get_stream_ctrl_format_size(
            devh, &ctrl, UVC_FRAME_FORMAT_MJPEG,
            1920, 1080, 30) < 0) {
        printf("Failed to get stream control\n");
        return -1;
    }
    
    // ---- START STREAM ----
    if (uvc_start_streaming(devh, &ctrl, cb, NULL, 0) < 0) {
        printf("Failed to start streaming\n");
        return -1;
    }

    printf("Streaming to /dev/video10...\n");


    while (!stop_flag) {
     sleep(1);  // wait until Ctrl+C
    }
     usleep(100*1000);
     if(devh) uvc_stop_streaming(&devh);
    if (v4l2_fd > 0) close(v4l2_fd);

    // ---- FREE BUFFERS ----
    if (yuyv_buf) {
      free(yuyv_buf);
      yuyv_buf = NULL;
    }

    if (rgb) {  // if you allocated a frame
    //  / uvc_free_frame(rgb);
      rgb = NULL;
    }
     
    return 0;
}