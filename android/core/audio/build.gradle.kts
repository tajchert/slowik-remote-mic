plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "pl.tajchert.slowik.audio"
    compileSdk = libs.versions.compileSdk.get().toInt()
    ndkVersion = "30.0.16248370"

    defaultConfig {
        minSdk = libs.versions.minSdk.get().toInt()
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_shared") } }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures { prefab = true }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation(libs.oboe)
}
