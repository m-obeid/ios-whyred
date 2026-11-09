#!/bin/bash
# Grab the phone screen from the laptop webcam: full frame plus an optional crop rotated upright.
#   camshot.sh OUT_PREFIX [CROP]     CROP = w:h:x:y in the 1920x1080 frame, e.g. 420:400:590:560
# Exposure 150 / gain 2 suits white text on black. The phone sits landscape, top to the left.
set -euo pipefail
out=$1 crop=${2:-}
v4l2-ctl -d /dev/video0 -c auto_exposure=1 -c exposure_time_absolute=${EXP:-150} -c gain=${GAIN:-2} -c backlight_compensation=0
ffmpeg -hide_banner -loglevel error -f v4l2 -input_format mjpeg -video_size 1920x1080 -i /dev/video0 \
	-vf "select=gte(n\,10)" -frames:v 1 -y "$out-full.jpg"
if [ -n "$crop" ]; then
	ffmpeg -hide_banner -loglevel error -i "$out-full.jpg" \
		-vf "crop=$crop,transpose=1,scale=iw*3:ih*3:flags=lanczos" -y "$out-crop.png"
fi
ls -l "$out"-*
