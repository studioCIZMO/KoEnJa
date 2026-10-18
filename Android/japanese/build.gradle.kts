plugins { kotlin("jvm") }
kotlin { jvmToolchain(17) }
dependencies {
    implementation(project(":engine"))
    testImplementation(kotlin("test"))
}
tasks.test { useJUnitPlatform() }
