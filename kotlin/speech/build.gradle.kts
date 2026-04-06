import com.android.build.api.dsl.LibraryExtension

plugins {
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.kotlinx.serialization)
    id("org.jetbrains.compose")
    id("com.android.library")
    alias(libs.plugins.vanniktech.publish)
}

group = "dev.deviceai"
version = (System.getenv("RELEASE_VERSION") ?: "0.2.0-alpha02")

kotlin {
    // Use JVM toolchain for consistent Java version
    jvmToolchain(17)

    // Android target
    androidTarget {
        publishLibraryVariants("release")
    }

    // JVM target for desktop
    jvm()

    val isMacOs = org.gradle.internal.os.OperatingSystem.current().isMacOsX

    if (isMacOs) {

    // Tool finder utility
    fun findTool(name: String, extraCandidates: List<String> = emptyList()): String {
        System.getenv("${name.uppercase()}_PATH")?.let { if (file(it).canExecute()) return it }
        val candidates = mutableListOf(
            "/opt/homebrew/bin/$name",
            "/usr/local/bin/$name",
            "/usr/bin/$name"
        )
        candidates.addAll(extraCandidates)
        try {
            val out = providers.exec { commandLine("which", name) }
                .standardOutput.asText.get().trim()
            if (out.isNotEmpty() && file(out).canExecute()) return out
        } catch (_: Throwable) {}
        for (p in candidates) if (file(p).canExecute()) return p
        throw GradleException(
            "Cannot find required tool '$name'. " +
                "Install it (e.g. 'brew install $name') or set ${name.uppercase()}_PATH=/full/path/to/$name"
        )
    }

    val cmakePath = findTool("cmake")

    // Desktop JNI build
    val macJniBuildDir = layout.buildDirectory
        .dir("speech-jni/macos")
        .get()
        .asFile

    val buildSpeechJniDesktop by tasks.registering(Exec::class) {
        group = "speech-native"
        description = "Configure CMake for desktop speech_jni"

        doFirst {
            val sourceDir = projectDir.resolve("cmake/speech-jni-desktop")
            macJniBuildDir.mkdirs()

            commandLine(
                cmakePath,
                "-S", sourceDir.absolutePath,
                "-B", macJniBuildDir.absolutePath,
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_SYSTEM_NAME=Darwin"
            )
        }
    }

    val compileSpeechJniDesktop by tasks.registering(Exec::class) {
        group = "speech-native"
        description = "Build desktop libspeech_jni.dylib"
        dependsOn(buildSpeechJniDesktop)

        commandLine(
            cmakePath,
            "--build", macJniBuildDir.absolutePath,
            "--config", "Release"
        )
    }

    } // end isMacOs

    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlin.stdlib)
            implementation(compose.runtime)
            implementation(compose.ui)
            implementation(libs.kotlinx.coroutines.core)
            implementation(libs.ktor.client.core)
            implementation(libs.ktor.client.content.negotiation)
            implementation(libs.ktor.serialization.kotlinx.json)
            implementation(libs.kotlinx.serialization.json)
            api(project(":kotlin:core"))
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

// Build desktop JNI with main build
tasks.named("build").configure {
    dependsOn("compileSpeechJniDesktop")
}

// Android configuration
extensions.configure<LibraryExtension> {
    namespace = "dev.deviceai"
    compileSdk = libs.versions.android.compileSdk.get().toInt()

    defaultConfig {
        minSdk = libs.versions.android.minSdk.get().toInt()
        ndk {
            abiFilters += setOf("arm64-v8a", "x86_64")
        }
    }

    buildTypes {
        release { isMinifyEnabled = false }
        debug { isMinifyEnabled = false }
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
                // Always build native code in Release mode regardless of the Android variant.
                // Without this, AGP passes -DCMAKE_BUILD_TYPE=Debug for assembleDebug, which
                // compiles whisper.cpp / ggml with -O0 — making inference 10-20x slower.
                arguments("-DCMAKE_BUILD_TYPE=Release")
            }
        }
    }

}

mavenPublishing {
    publishToMavenCentral(com.vanniktech.maven.publish.SonatypeHost.CENTRAL_PORTAL, automaticRelease = true)
    signAllPublications()

    coordinates("dev.deviceai", "speech", version.toString())

    pom {
        name.set("DeviceAI Runtime — Speech")
        description.set("Kotlin Multiplatform library for on-device Speech-to-Text and Text-to-Speech")
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
