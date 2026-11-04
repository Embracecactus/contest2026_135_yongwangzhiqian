# Source provenance

## Project code

The application, protocol, Gateway-client, resource and unit-test sources in
this directory are native Kotlin/Android project code authored for Shaniu and
carry `SPDX-License-Identifier: Apache-2.0` markers. No third-party source
trees, credentials or private device captures are included here.
The public builtin WKM wake-model package is versioned under
`app/src/main/assets/wake-models`; its training/model provenance is maintained
in the root `SOURCE_PROVENANCE.md` and `app/bk7258/models` metadata.

## Current artwork and demonstration

The launcher foreground is an original AI-generated fictional adult character.
The built-in image generation tool created it from text without a reference
photograph or a real person's likeness. The transparent original, exact prompt
and SHA256 are preserved in `design/launcher-20261104/`. A 512px WebP export is
packaged as `res/drawable-nodpi/shaniu_launcher_response.webp`; the adaptive
insets, background and monochrome bob-hair silhouette are original Android XML
resources (Apache-2.0). The earlier ruby portrait is retired from the APK; its
historical prompt and SHA256 remain in the root source record.
Public video covers are supplied by the project author with the competition
demonstration; they are not launcher art.
The App README links the full supplementary video and preserves the distinction
between showing the OTA entry and recording an actual upgrade.

## Gradle wrapper

`gradle/wrapper/gradle-wrapper.properties` declares the official Gradle 8.13
binary distribution at `https://services.gradle.org/distributions/gradle-8.13-bin.zip`
and pins its distribution SHA-256 as
`20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78`.
The wrapper scripts retain their upstream Apache-2.0 copyright notices.

The repository wrapper JAR SHA-256 is
`81a82aaea5abcc8ff68b3dfcb58b3c3c429378efd98e7433460610fecd7ae45f`.
On 2026-09-07, the locally extracted Gradle 8.13 cache's
`lib/plugins/gradle-wrapper-main-8.13.jar` was inspected as a ZIP archive.
Its embedded `gradle-wrapper.jar` has the same SHA-256, which verifies the
repository JAR against that installed Gradle 8.13 distribution. The outer
JAR's SHA-256 is
`78e3fe4f5eb0121f818ef2563eec8e6a7facf24203b7690fcdf9679befc5ae22`.

No `gradlew` or `gradlew.bat` wrapper-template copy was present in the local
Gradle cache, so those scripts were not byte-compared. They retain the
upstream Apache-2.0 copyright and SPDX notices; their origin is declared by
the wrapper configuration and script headers.
