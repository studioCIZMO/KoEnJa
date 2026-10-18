pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositories { google(); mavenCentral() }
}
rootProject.name = "REA"
// engine: Kotlin port of SeamtypeCore (plain JVM, tested against tools/golden's output)
// japanese: kana → kanji conversion; app: the keyboard (InputMethodService) and its settings
include(":engine", ":japanese", ":app")
