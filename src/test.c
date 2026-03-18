/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (C) 2010-2012 Ken Tossell
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the author nor other contributors may be
*     used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/
#include <stdio.h>

#include "libuvc/libuvc.h"
#include <stdint.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

#define PORT 8080

static int server_fd = -1;
static int client_fd = -1;
static uint8_t *latest_frame = NULL;
static size_t latest_frame_size = 0;
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static uvc_device_handle_t *g_devh = NULL;
// Stream thread
void *stream_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&frame_mutex);
        if (client_fd > 0 && latest_frame && latest_frame_size > 0) {
            char part[256];
            int n = snprintf(part, sizeof(part),
                             "--frame\r\n"
                             "Content-Type: image/jpeg\r\n"
                             "Content-Length: %zu\r\n\r\n",
                             latest_frame_size);
            send(client_fd, part, n, 0);
            send(client_fd, latest_frame, latest_frame_size, 0);
            send(client_fd, "\r\n", 2, 0);
        }
        pthread_mutex_unlock(&frame_mutex);
        usleep(33000); // ~30 FPS
    }
    return NULL;
}

// Start HTTP MJPEG server
void start_server() {
    struct sockaddr_in addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return; }
    listen(server_fd, 1);

    printf("Waiting for client on http://localhost:%d\n", PORT);
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) { perror("accept"); return; }

    const char *header =
        "HTTP/1.0 200 OK\r\n"
        "Connection: close\r\n"
        "Max-Age: 0\r\n"
        "Expires: 0\r\n"
        "Cache-Control: no-cache, private\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    send(client_fd, header, strlen(header), 0);

    pthread_t tid;
    pthread_create(&tid, NULL, stream_thread, NULL);
}

// Libuvc callback
void cb(uvc_frame_t *frame, void *ptr) {
    static uint64_t total_frames = 0;
    static uint64_t window_frames = 0;
    static int initialized = 0;
    static struct timeval start_tv, last_tv;
    static int server_started = 0;

    struct timeval now;
    double total_elapsed, window_elapsed, fps_avg, fps_inst;

    if (!initialized) {
        gettimeofday(&start_tv, NULL);
        last_tv = start_tv;
        initialized = 1;
        return;
    }

    gettimeofday(&now, NULL);
    total_frames++;
    window_frames++;

    total_elapsed = (double)(now.tv_sec - start_tv.tv_sec) + 
                    (double)(now.tv_usec - start_tv.tv_usec)/1000000.0;
    window_elapsed = (double)(now.tv_sec - last_tv.tv_sec) + 
                     (double)(now.tv_usec - last_tv.tv_usec)/1000000.0;

    fps_avg = total_elapsed > 0 ? total_frames / total_elapsed : 0.0;
    fps_inst = window_elapsed > 0 ? window_frames / window_elapsed : 0.0;

//    printf("%llu: callback! length = %u, ptr=%p\n", 
 //          (unsigned long long)total_frames, (unsigned)frame->data_bytes, ptr);
 //   printf("width=%u, height=%u\n", frame->width, frame->height);
  //  printf("format=%d, size=%zu\n", frame->frame_format, frame->data_bytes);

    if (window_elapsed >= 1.0) {
    //    printf("FPS(inst)=%.2f, FPS(avg)=%.2f\n", fps_inst, fps_avg);
        last_tv = now;
        window_frames = 0;
    }

    // Start server once
    if (!server_started) {
        server_started = 1;
        start_server();
    }

    // Copy frame to latest_frame buffer
    pthread_mutex_lock(&frame_mutex);
    free(latest_frame);
    latest_frame = malloc(frame->data_bytes);
    if (latest_frame) {
        memcpy(latest_frame, frame->data, frame->data_bytes);
        latest_frame_size = frame->data_bytes;
    } else {
        latest_frame_size = 0;
    }
    pthread_mutex_unlock(&frame_mutex);
}

void sigint_handler(int sig) {
    if (g_devh) {
        uvc_stop_streaming(g_devh);
        puts("Streaming stopped by Ctrl+C");
    }
    uvc_close(g_devh);
    exit(0);
}

int main(int argc, char **argv) {
  uvc_context_t *ctx;
  uvc_error_t res;
  uvc_device_t *dev;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;

  signal(SIGINT,sigint_handler);
  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  puts("UVC initialized");

  res = uvc_find_device(
      ctx, &dev,
      16981, //vid
      24065, //pid
      NULL);


  if (res < 0) {
    uvc_perror(res, "uvc_find_device");
  } else {
    puts("Device found");

    res = uvc_open(dev, &devh);

    if (res < 0) {
      uvc_perror(res, "uvc_open");
    } else {
      puts("Device opened");

      uvc_print_diag(devh, stderr);

      res = uvc_get_stream_ctrl_format_size(
          devh, &ctrl, UVC_FRAME_FORMAT_MJPEG, 1920, 1080, 30
      );

      uvc_print_stream_ctrl(&ctrl, stderr);

      if (res < 0) {
        uvc_perror(res, "get_mode");
      } else {
        g_devh=devh;
        res = uvc_start_streaming(devh, &ctrl, cb, 12345, 0);

        if (res < 0) {
          uvc_perror(res, "start_streaming");
        }
        else{
          puts("Streaming... Press Ctrl+C to stop.");

          while (1) sleep(1); // keep program alive
        }
      uvc_close(devh);
      puts("Device closed");
    }

    uvc_unref_device(dev);
  }

  uvc_exit(ctx);
  puts("UVC exited");

  return 0;
  }
}

