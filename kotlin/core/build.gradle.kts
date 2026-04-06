import com.android.build.api.dsl.LibraryExtension

plugins {
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.kotlinx.serialization)
    id("com.android.library")
    alias(libs.plugins.vanniktech.publish)
}

group = "dev.deviceai"
version = (System.getenv("RELEASE_VERSION") ?: "0.2.0-alpha02")

kotlin {
    jvmToolchain(17)

    androidTarget {
        publishLibraryVariants("release")
    }

    jvm()

    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlin.stdlib)
            implementation(libs.kotlinx.coroutines.core)
            implementation(libs.ktor.client.core)
            implementation(libs.ktor.client.content.negotiation)
            implementation(libs.ktor.serialization.kotlinx.json)
            implementation(libs.kotlinx.serialization.json)
        }
        commonTest.dependencies {
            implementation(libs.kotlin.test)
        }
        androidMain.dependencies {
            implementation(libs.ktor.client.okhttp)
        }
        jvmMain.dependencies {
            implementation(libs.ktor.client.okhttp)
        }
    }
}

extensions.configure<LibraryExtension> {
    namespace  = "dev.deviceai.core"
    compileSdk = libs.versions.android.compileSdk.get().toInt()

    defaultConfig {
        minSdk = libs.versions.android.minSdk.get().toInt()
    }

    buildTypes {
        release { isMinifyEnabled = false }
        debug   { isMinifyEnabled = false }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

mavenPublishing {
    publishToMavenCentral(com.vanniktech.maven.publish.SonatypeHost.CENTRAL_PORTAL, automaticRelease = true)
    signAllPublications()

    coordinates("dev.deviceai", "core", version.toString())

    pom {
        name.set("DeviceAI Runtime — Core")
        description.set("Shared model management, storage, download, and logging for DeviceAI modules")
        url.set("https://github.com/deviceai-labs/runtime-kmp")
        licenses {
            license {
                name.set("Apache-2.0")
                url.set("https://www.apache.org/licenses/LICENSE-2.0")
            }
        }
        developers {
            developer {
                id.set("nikhilbhutani")
                name.set("Nikhil Bhutani")
                url.set("https://github.com/NikhilBhutani")
            }
        }
        scm {
            url.set("https://github.com/deviceai-labs/runtime-kmp")
            connection.set("scm:git:git://github.com/deviceai-labs/runtime-kmp.git")
            developerConnection.set("scm:git:ssh://github.com/deviceai-labs/runtime-kmp.git")
        }
    }
}
