plugins { id("com.android.application") }
android {
    namespace = "com.netmaru.rea"
    compileSdk = 36
    defaultConfig { applicationId = "com.netmaru.rea"; minSdk = 26; targetSdk = 36; versionCode = 1; versionName = "0.1" }
}
dependencies { testImplementation("junit:junit:4.13.2") }
