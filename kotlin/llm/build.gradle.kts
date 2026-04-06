import com.android.build.api.dsl.LibraryExtension
import org.jetbrains.kotlin.gradle.ExperimentalKotlinGradlePluginApi

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

    // Shared jvmCommon source set — avoids copy-paste between androidMain and jvmMain.
    @OptIn(ExperimentalKotlinGradlePluginApi::class)
    applyDefaultHierarchyTemplate {
        common {
            group("jvmCommon") {
                withAndroidTarget()
                withJvm()
            }
        }
    }

    androidTarget {
        publishLibraryVariants("release")
    }

    jvm()

    val isMacOs = org.gradle.internal.os.OperatingSystem.current().isMacOsX

    if (isMacOs) {

    fun findTool(name: String): String {
        System.getenv("${name.uppercase()}_PATH")?.let { if (file(it).canExecute()) return it }
        val candidates = listOf("/opt/homebrew/bin/$name", "/usr/local/bin/$name", "/usr/bin/$name")
        try {
            val out = providers.exec { commandLine("which", name) }
                .standardOutput.asText.get().trim()
            if (out.isNotEmpty() && file(out).canExecute()) return out
        } catch (_: Throwable) {}
        for (p in candidates) if (file(p).canExecute()) return p
        throw GradleException("Cannot find '$name'. Install via 'brew install $name'.")
    }

    val cmakePath = findTool("cmake")

    // Desktop JNI build
    val macJniBuildDir = layout.buildDirectory.dir("llm-jni/macos").get().asFile

    val buildLlmJniDesktop by tasks.registering(Exec::class) {
        group = "llm-native"
        doFirst {
            macJniBuildDir.mkdirs()
            commandLine(cmakePath, "-S",
                projectDir.resolve("cmake/llm-jni-desktop").absolutePath,
                "-B", macJniBuildDir.absolutePath,
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_SYSTEM_NAME=Darwin"
            )
        }
    }

    val compileLlmJniDesktop by tasks.registering(Exec::class) {
        group = "llm-native"
        dependsOn(buildLlmJniDesktop)
        commandLine(cmakePath, "--build", macJniBuildDir.absolutePath, "--config", "Release")
    }

    tasks.named("build").configure { dependsOn("compileLlmJniDesktop") }

    } // end isMacOs

    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlin.stdlib)
            implementation(libs.kotlinx.coroutines.core)
            implementation(libs.ktor.client.core)
            implementation(libs.ktor.client.content.negotiation)
            implementation(libs.ktor.serialization.kotlinx.json)
            implementation(libs.kotlinx.serialization.json)
            implementation(project(":kotlin:core"))
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
    namespace  = "dev.deviceai.llm"
    compileSdk = libs.versions.android.compileSdk.get().toInt()

    defaultConfig {
        minSdk = libs.versions.android.minSdk.get().toInt()
        ndk { abiFilters += setOf("arm64-v8a", "x86_64") }
    }

    buildTypes {
        release { isMinifyEnabled = false }
        debug   { isMinifyEnabled = false }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs { useLegacyPackaging = false }
    }

    externalNativeBuild {
        cmake {
            path = file("src/commonMain/cpp/CMakeLists.txt")
        }
    }

    defaultConfig {
        externalNativeBuild {
            cmake {
                arguments("-DCMAKE_BUILD_TYPE=Release")
            }
        }
    }
}

mavenPublishing {
    publishToMavenCentral(com.vanniktech.maven.publish.SonatypeHost.CENTRAL_PORTAL, automaticRelease = true)
    signAllPublications()

    coordinates("dev.deviceai", "llm", version.toString())

    pom {
        name.set("DeviceAI Runtime — LLM")
        description.set("Kotlin Multiplatform library for on-device LLM inference via llama.cpp")
        url.set("https://github.com/deviceai-labs/deviceai")
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
            url.set("https://github.com/deviceai-labs/deviceai")
            connection.set("scm:git:git://github.com/deviceai-labs/deviceai.git")
            developerConnection.set("scm:git:ssh://github.com/deviceai-labs/deviceai.git")
        }
    }
}
