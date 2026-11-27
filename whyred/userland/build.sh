#!/bin/sh
# whyred_ui.dylib without an iOS SDK: clang for the object, ld64.lld against the text stubs.
set -e
cd "$(dirname "$0")"
mkdir -p out
clang -target arm64-apple-ios16.0 -O2 -fno-stack-protector -ffreestanding -fvisibility=hidden -c whyred_ui.c -o out/whyred_ui.o
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -dylib -install_name /usr/lib/whyred_ui.dylib \
	-adhoc_codesign out/whyred_ui.o stubs/libobjc.tbd stubs/libSystem.tbd stubs/IOKit.tbd stubs/CoreFoundation.tbd stubs/AudioToolbox.tbd -o out/whyred_ui.dylib
ls -l out/whyred_ui.dylib
# vibe: test tool, AudioServicesPlaySystemSound(kSystemSoundID_Vibrate) (run it with whyred_ui injected)
clang -target arm64-apple-ios16.0 -O2 -c tools/vibe.c -o out/vibe.o
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -adhoc_codesign out/vibe.o stubs/libSystem.tbd stubs/AudioToolbox.tbd -o out/vibe
# pcmplay: raw 48 kHz 16-bit stereo to /dev/topaz-audio at the real-time pace
clang -target arm64-apple-ios16.0 -O2 -c tools/pcmplay.c -o out/pcmplay.o
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -adhoc_codesign out/pcmplay.o stubs/libSystem.tbd -o out/pcmplay
# micrec: raw 48 kHz 16-bit mono from /dev/redwood-mic
clang -target arm64-apple-ios16.0 -O2 -c tools/micrec.c -o out/micrec.o
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -adhoc_codesign out/micrec.o stubs/libSystem.tbd -o out/micrec
# gputest: the RedwoodGPU service without Mesa (patch 1033; boot-arg gpu=1 or whyredgpusvc=1)
clang -target arm64-apple-ios16.0 -O2 -c tools/gputest.c -o out/gputest.o
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -adhoc_codesign out/gputest.o stubs/libSystem.tbd stubs/IOKit.tbd -o out/gputest
