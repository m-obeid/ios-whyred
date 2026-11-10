#!/bin/sh
# whyred_ui.dylib without an iOS SDK: clang for the object, ld64.lld against the text stubs.
set -e
cd "$(dirname "$0")"
mkdir -p out
clang -target arm64-apple-ios16.0 -O2 -fno-stack-protector -ffreestanding -fvisibility=hidden -c whyred_ui.c -o out/whyred_ui.o
ld64.lld -arch arm64 -platform_version ios 16.0 16.0 -dylib -install_name /usr/lib/whyred_ui.dylib \
	-adhoc_codesign out/whyred_ui.o stubs/libobjc.tbd stubs/libSystem.tbd -o out/whyred_ui.dylib
ls -l out/whyred_ui.dylib
