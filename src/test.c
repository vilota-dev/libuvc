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


void cb(uvc_frame_t *frame, void *ptr) {
  uvc_frame_t *bgr;

  static uint64_t total_frames = 0;
  static uint64_t window_frames = 0;
  static int initialized = 0;
  static struct timeval start_tv;
  static struct timeval last_tv;

  struct timeval now;
  double total_elapsed;
  double window_elapsed;
  double fps_avg;
  double fps_inst;

  if (!initialized) {
    gettimeofday(&start_tv, NULL);
    last_tv = start_tv;
    initialized = 1;
    return;
  }

  gettimeofday(&now, NULL);

  total_frames++;
  window_frames++;

  total_elapsed =
      (double)(now.tv_sec - start_tv.tv_sec) +
      (double)(now.tv_usec - start_tv.tv_usec) / 1000000.0;

  window_elapsed =
      (double)(now.tv_sec - last_tv.tv_sec) +
      (double)(now.tv_usec - last_tv.tv_usec) / 1000000.0;

  fps_avg = (total_elapsed > 0.0) ? ((double)total_frames / total_elapsed) : 0.0;
  fps_inst = (window_elapsed > 0.0) ? ((double)window_frames / window_elapsed) : 0.0;

  printf("%llu: callback! length = %u, ptr = %p\n",
         (unsigned long long)total_frames,
         (unsigned)frame->data_bytes,
         ptr);
  printf("width %u, height %u\n",
         (unsigned)frame->width,
         (unsigned)frame->height);

  // Print FPS once per second to avoid spamming too much
  if (window_elapsed >= 1.0) {
    printf("FPS(inst)=%.2f, FPS(avg)=%.2f\n", fps_inst, fps_avg);
    last_tv = now;
    window_frames = 0;
  }

  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!bgr) {
    printf("unable to allocate bgr frame!\n");
    return;
  }

  // ret = uvc_any2bgr(frame, bgr);
  // if (ret) {
  //   uvc_perror(ret, "uvc_any2bgr");
  //   uvc_free_frame(bgr);
  //   return;
  // }

  // cvWaitKey(10);

  uvc_free_frame(bgr);
}

int main(int argc, char **argv) {
  uvc_context_t *ctx;
  uvc_error_t res;
  uvc_device_t *dev;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;

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
        res = uvc_start_streaming(devh, &ctrl, cb, 12345, 0);

        if (res < 0) {
          uvc_perror(res, "start_streaming");
        } else {
          puts("Streaming for 10 seconds...");
          // uvc_error_t resAEMODE = uvc_set_ae_mode(devh, 1);
          // uvc_perror(resAEMODE, "set_ae_mode");
          int i;
          for (i = 1; i <= 10; i++) {
            /* uvc_error_t resPT = uvc_set_pantilt_abs(devh, i * 20 * 3600, 0); */
            /* uvc_perror(resPT, "set_pt_abs"); */
            // uvc_error_t resEXP = uvc_set_exposure_abs(devh, 20 + i * 5);
            // uvc_perror(resEXP, "set_exp_abs");
            
            sleep(1);
          }
          sleep(10);
          uvc_stop_streaming(devh);
	  puts("Done streaming.");
        }
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

