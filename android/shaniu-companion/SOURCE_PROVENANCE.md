# Source provenance

## Project code

The application, protocol, Gateway-client, resource and unit-test sources in
this directory are native Kotlin/Android project code authored for Shaniu and
carry `SPDX-License-Identifier: Apache-2.0` markers. No third-party source
trees, model assets, credentials or device captures are included here.

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
