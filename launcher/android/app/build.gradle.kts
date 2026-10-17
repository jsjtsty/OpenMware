plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "org.openmware.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.openmware.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
    }

    sourceSets {
        getByName("main") {
            // sdl-java: SDL's Java classes, copied from the SDL source CMake fetched (matches libSDL3.so exactly).
            // jniLibs / assets: libmain.so, libSDL3.so, FFmpeg's libraries, the SPIR-V shaders, licence texts.
            // All three are filled by launcher/android/prepare_native.sh.
            java.srcDirs("src/main/java", "src/sdl-java")
            jniLibs.srcDirs("src/main/jniLibs")
            assets.srcDirs("src/main/assets")
        }
    }

    buildFeatures { compose = true }

    packaging {
        jniLibs { useLegacyPackaging = false }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Debug-signed so the release APK installs for testing; real distribution needs its own keystore.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.documentfile:documentfile:1.0.1")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
