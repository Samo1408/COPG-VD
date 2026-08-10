plugins {
    id("com.android.application")
}

android {
    namespace = "com.copgvd.xposed"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.copgvd.xposed"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

dependencies {
    compileOnly("de.robv.android.xposed:api:82")
}
