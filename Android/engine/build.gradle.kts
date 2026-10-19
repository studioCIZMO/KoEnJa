plugins { kotlin("jvm") }
kotlin { jvmToolchain(17) }
dependencies { testImplementation(kotlin("test")) }
tasks.test {
    useJUnitPlatform()
    // The models' data files, read in place from the Swift package (Resources.open's default).
    systemProperty("rea.data", rootDir.resolve("../Packages/SeamtypeCore/Sources/SeamtypeCore/Data").canonicalPath)
    testLogging { showStandardStreams = true }
}
