# SPDX-License-Identifier: Apache-2.0
# Open the built deck in PowerPoint, export every slide (hidden included) to PNG
# and one static PDF that also contains the hidden appendix slides.
# Usage (from WSL): powershell.exe -NoProfile -ExecutionPolicy Bypass -File render.ps1 -Pptx <win> -Out <win dir>
param([string]$Pptx, [string]$Out)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$app = New-Object -ComObject PowerPoint.Application
try {
  $pres = $app.Presentations.Open($Pptx, $true, $false, $false)
  foreach ($s in $pres.Slides) {
    $s.Export((Join-Path $Out ("slide{0:D2}.png" -f $s.SlideIndex)), "PNG", 1920, 1080)
    # Unhide only in this read-only session so the PDF keeps the appendix.
    $s.SlideShowTransition.Hidden = 0
  }
  $pres.SaveAs((Join-Path $Out "deck.pdf"), 32)
  "slides=" + $pres.Slides.Count
  foreach ($f in $pres.Fonts) { "font " + $f.Name + " embedded=" + $f.Embedded }
  $pres.Close()
} finally {
  $app.Quit()
}
