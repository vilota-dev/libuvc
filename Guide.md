
To create virtual v4l2 node excute below two commands:

sudo modprobe -r v4l2loopback

sudo modprobe v4l2loopback devices=1 video_nr=10 card_label="UVC Virtual Cam" exclusive_caps=1


To view video 10 in gst :

gst-launch-1.0 -v v4l2src device=/dev/video10 !     video/x-raw,format=YUY2,width=1920,height=1080,framerate=30/1 !     videoconvert ! fpsdisplaysink text-overlay=true
