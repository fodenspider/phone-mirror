plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }

android {
    namespace = "io.github.fodenspider.phonemirror"
    compileSdk = 35
    defaultConfig {
        applicationId = "io.github.fodenspider.phonemirror"
        minSdk = 30
        targetSdk = 35
        versionCode = 2
        versionName = "0.2.0-usb-preview"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    sourceSets.getByName("test").resources.srcDir("../../protocol/fixtures")
    lint { abortOnError = true }
}
dependencies { testImplementation("junit:junit:4.13.2") }
