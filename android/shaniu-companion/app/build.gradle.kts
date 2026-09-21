// SPDX-License-Identifier: Apache-2.0

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.shaniu.companion"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.shaniu.companion"
        minSdk = 29
        targetSdk = 35
        versionCode = 29
        versionName = "0.6.0-shaniu-firstboot"
        buildConfigField("boolean", "LEGACY_SERVICE_DEMO", "false")
        testInstrumentationRunner = "com.shaniu.companion.provision.ControlKeyInstrumentation"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = true
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    sourceSets.getByName("test").resources.srcDir(
        rootProject.projectDir.resolve("../../gateway/shaniu/tests/fixtures"),
    )
}

dependencies {
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
    implementation("com.google.code.gson:gson:2.11.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    testImplementation("junit:junit:4.13.2")
}

// Optional native peer participates in the test cache identity; a skipped
// run must never satisfy a subsequent cross-language verification request.
tasks.withType<org.gradle.api.tasks.testing.Test>().configureEach {
    listOf("SHANIU_CONTROL_PEER", "SHANIU_CONTROL_TLS_PEER").forEach { name ->
        val peer = providers.environmentVariable(name)
        inputs.property(name, peer.orElse(""))
        if (peer.isPresent) inputs.file(peer.get())
    }
}
