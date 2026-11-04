#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Final assembly: clean picture + master audio → (1) clean MP4 for editing,
# (2) delivery MP4 with burned subtitles. Refuses to overwrite.
# Usage: tools/finalize.sh <clean-picture.mp4> <out-dir>
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
pic="$(realpath "$1")"; out="$2"; mkdir -p "$out"; out="$(realpath "$out")"
clean="$out/wozai-v3.1-clean.mp4"; final="$out/wozai-v3.1.mp4"
for f in "$clean" "$final"; do [ -e "$f" ] && { echo "exists, refusing to overwrite: $f"; exit 1; }; done
master="$here/audio/master/master.wav"; ass="$here/subtitles/wozai.zh-Hans.ass"; fonts="$here/assets/fonts"
# Clean: picture stream copied, master as AAC 320k.
ffmpeg -v error -n -i "$pic" -i "$master" -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 320k -ar 48000 -shortest -movflags +faststart "$clean"
# Delivery: subtitles burned with the project's Noto Sans SC.
ffmpeg -v error -n -i "$pic" -i "$master" -map 0:v:0 -map 1:a:0 \
  -vf "subtitles=filename='$ass':fontsdir='$fonts'" -c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p -profile:v high \
  -c:a aac -b:a 320k -ar 48000 -shortest -movflags +faststart "$final"
echo "clean: $clean"; echo "final: $final"
