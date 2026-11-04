#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Replace the tail of a full picture render, from a frame boundary onward, with a
# separately rendered segment (index-range.html / index-endcard.html with
# data-film-offset equal to <at>). Used when a late fix touches only the end of
# the film, so the ~1.5 h full render need not be repeated.
#
# The cut is frame-exact: frames [0, at*30) from <full>, then every frame of
# <tail>. One re-encode at CRF 14 (visually transparent for this material);
# BT.709 tags kept. Refuses to overwrite.
# Usage: tools/splice.sh <full.mp4> <tail.mp4> <at-seconds> <out.mp4>
set -euo pipefail
full="$1"; tail="$2"; at="$3"; out="$4"
[ -e "$out" ] && { echo "exists, refusing to overwrite: $out"; exit 1; }
fps=30
n=$(python3 -c "import sys; v=float(sys.argv[1])*$fps; assert abs(v-round(v))<1e-6, 'cut is not on a frame boundary'; print(round(v))" "$at")
nf=$(ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 "$full")
nt=$(ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 "$tail")
echo "full $nf frames; keep 0..$((n-1)); tail $nt frames; result $((n+nt)) frames"
ffmpeg -v error -n -i "$full" -i "$tail" -filter_complex \
  "[0:v]trim=end_frame=$n,setpts=PTS-STARTPTS[a];[1:v]setpts=PTS-STARTPTS[b];[a][b]concat=n=2:v=1:a=0,format=yuv420p[v]" \
  -map "[v]" -r $fps -c:v libx264 -preset slow -crf 14 -g 120 -profile:v high \
  -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv -movflags +faststart "$out"
got=$(ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 "$out")
[ "$got" -eq $((n+nt)) ] || { echo "frame count $got != $((n+nt))"; exit 1; }
echo "spliced: $out ($got frames)"
