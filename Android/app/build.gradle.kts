plugins { id("com.android.application") }
android {
    namespace = "com.netmaru.rea"
    compileSdk = 36
    defaultConfig { applicationId = "com.netmaru.rea"; minSdk = 26; targetSdk = 36; versionCode = 1; versionName = "0.1" }
    // the engine's data files, packaged from the Swift package where they live
    sourceSets["main"].assets.srcDir("../../Packages/SeamtypeCore/Sources/SeamtypeCore/Data")
    testOptions.unitTests.all {
        it.systemProperty("rea.data", file("../../Packages/SeamtypeCore/Sources/SeamtypeCore/Data").canonicalPath)
    }
}
dependencies {
    implementation(project(":engine"))
    implementation(project(":japanese"))
    testImplementation("junit:junit:4.13.2")
}
