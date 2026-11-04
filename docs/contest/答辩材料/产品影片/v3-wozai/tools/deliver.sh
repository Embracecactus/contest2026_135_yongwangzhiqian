#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copy the finished v3.1 outputs into the delivery folder under new names.
# Never overwrites: any existing target stops the script before copying.
# Usage: tools/deliver.sh <final-dir with wozai-v3.1*.mp4 and 交付说明.txt> <covers-dir> <delivery-dir>
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
fin="$(realpath "$1")"; cov="$(realpath "$2")"; dst="$3"
p="傻妞影片-我在-v3.1"
declare -A map=(
  ["$fin/wozai-v3.1.mp4"]="$p.mp4"
  ["$fin/wozai-v3.1-clean.mp4"]="$p-无字幕画面.mp4"
  ["$here/subtitles/wozai.zh-Hans.srt"]="$p.zh-Hans.srt"
  ["$here/subtitles/wozai.zh-Hans.vtt"]="$p.zh-Hans.vtt"
  ["$here/subtitles/wozai.zh-Hans.ass"]="$p.zh-Hans.ass"
  ["$here/audio/master/master.wav"]="$p-分轨/母带-master.wav"
  ["$here/audio/stems/narration.wav"]="$p-分轨/旁白-narration.wav"
  ["$here/audio/stems/dialogue.wav"]="$p-分轨/对白与设备声-dialogue.wav"
  ["$here/audio/stems/music.wav"]="$p-分轨/配乐-music.wav"
  ["$here/audio/stems/sfx.wav"]="$p-分轨/音效-sfx.wav"
  ["$cov/thumbnail-16x9.png"]="$p-缩略图-16x9.png"
  ["$cov/cover-4x3.png"]="$p-封面-4x3.png"
  ["$cov/cover-3x4.png"]="$p-封面-3x4.png"
  ["$cov/cover-9x16.png"]="$p-封面-9x16.png"
  ["$here/SCREENPLAY.md"]="$p-剧本.md"
  ["$here/STORYBOARD.md"]="$p-分镜表.md"
  ["$here/FEATURE_MAP.md"]="$p-功能与镜头对照.md"
  ["$here/REVIEW.md"]="$p-检查记录.md"
  ["$fin/交付说明.txt"]="$p-交付说明.txt"
)
for src in "${!map[@]}"; do
  [ -f "$src" ] || { echo "missing: $src"; exit 1; }
  [ -e "$dst/${map[$src]}" ] && { echo "exists, refusing to overwrite: $dst/${map[$src]}"; exit 1; }
done
[ -e "$dst/$p-SHA256SUMS.txt" ] && { echo "exists: $dst/$p-SHA256SUMS.txt"; exit 1; }
mkdir -p "$dst/$p-分轨"
for src in "${!map[@]}"; do cp -n "$src" "$dst/${map[$src]}"; done
( cd "$dst" && for src in "${!map[@]}"; do sha256sum "${map[$src]}"; done | sort -k2 > "$p-SHA256SUMS.txt" )
echo "delivered ${#map[@]} files to $dst"
