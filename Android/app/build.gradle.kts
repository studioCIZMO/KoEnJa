import java.util.Properties

plugins { id("com.android.application") }
// Play's upload key, kept off the repo: ~/.koenja/upload.properties (storeFile, storePassword, keyAlias, keyPassword).
val upload = Properties().apply { file("${System.getProperty("user.home")}/.koenja/upload.properties").takeIf { it.isFile }?.reader()?.use(::load) }
android {
    namespace = "com.netmaru.rea"
    compileSdk = 36
    defaultConfig { applicationId = "com.cizmo.koenja"; minSdk = 26; targetSdk = 36; versionCode = 3; versionName = "0.2.1" }
    if (upload.isNotEmpty()) {
        signingConfigs.create("upload") {
            storeFile = file(upload.getProperty("storeFile")); storePassword = upload.getProperty("storePassword")
            keyAlias = upload.getProperty("keyAlias"); keyPassword = upload.getProperty("keyPassword")
        }
        buildTypes["release"].signingConfig = signingConfigs["upload"]
    }
    // the engine's data files, packaged from the Swift package where they live
    sourceSets["main"].assets.srcDir("../../Packages/SeamtypeCore/Sources/SeamtypeCore/Data")
    // the open-source notices the settings screen shows (Araso's MIT License among them)
    sourceSets["main"].assets.srcDir("build/notices")
    testOptions.unitTests.all {
        it.systemProperty("rea.data", file("../../Packages/SeamtypeCore/Sources/SeamtypeCore/Data").canonicalPath)
    }
}
val notices = tasks.register<Copy>("notices") {
    from("../../THIRD_PARTY_NOTICES.txt", "../japanese/src/main/resources/mozc.dic.LICENSE.txt")
    into("build/notices")
}
tasks.named("preBuild") { dependsOn(notices) }
dependencies {
    implementation(project(":engine"))
    implementation(project(":japanese"))
    testImplementation("junit:junit:4.13.2")
}
