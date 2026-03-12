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
version = (System.getenv("RELEASE_VERSION") ?: "0.2.0-alpha01")

// Minimum iOS version
val minIos = "16.0"

kotlin {
    // Use JVM toolchain for consistent Java version
    jvmToolchain(17)

    // Android target
    androidTarget {
        publishLibraryVariants("release")
    }

    // JVM target for desktop
    jvm()

    // iOS targets
    iosX64()
    iosArm64()
    iosSimulatorArm64()

    targets.withType<org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget> {
        binaries.framework {
            baseName = "deviceai-runtime-kmp"
            isStatic = true
            linkerOpts("-Wl,-no_implicit_dylibs")
            freeCompilerArgs += listOf("-Xbinary=bundleId=dev.deviceai.library")
            freeCompilerArgs += "-Xoverride-konan-properties=osVersionMin.ios=$minIos"
        }
    }

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
    val libtoolPath = findTool("libtool")

    // iOS native builds
    listOf(
        Triple(iosX64(), "x86_64", "iPhoneSimulator"),
        Triple(iosArm64(), "arm64", "iPhoneOS"),
        Triple(iosSimulatorArm64(), "arm64", "iPhoneSimulator")
    ).forEach { (arch, archName, sdkName) ->
        val cmakeBuildDir = layout.buildDirectory
            .dir("speech-cmake/$sdkName/${arch.name}")
            .get()
            .asFile
        val buildTaskName = "buildSpeechCMake${arch.name.replaceFirstChar { it.uppercase() }}"

        tasks.register(buildTaskName, Exec::class) {
            doFirst {
                val sourceDir = projectDir.resolve("cmake/speech-wrapper-ios")
                val sdk = when (sdkName) {
                    "iPhoneSimulator" -> "iphonesimulator"
                    "iPhoneOS" -> "iphoneos"
                    else -> "macosx"
                }
                val sdkPathProvider = providers.exec {
                    commandLine("xcrun", "--sdk", sdk, "--show-sdk-path")
                }.standardOutput.asText.map { it.trim() }
                val systemName = if (sdk == "macosx") "Darwin" else "iOS"
                cmakeBuildDir.mkdirs()
                environment("PATH", "/opt/homebrew/bin:" + System.getenv("PATH"))

                commandLine = listOf(
                    cmakePath,
                    "-S", sourceDir.absolutePath,
                    "-B", cmakeBuildDir.absolutePath,
                    "-DCMAKE_SYSTEM_NAME=$systemName",
                    "-DCMAKE_OSX_ARCHITECTURES=$archName",
                    "-DCMAKE_OSX_SYSROOT=${sdkPathProvider.get()}",
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$minIos",
                    "-DCMAKE_INSTALL_PREFIX=${cmakeBuildDir.resolve("install")}",
                    "-DCMAKE_IOS_INSTALL_COMBINED=NO",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
                )
            }
        }

        val compileTask = tasks.register(
            "compileSpeechCMake${arch.name.replaceFirstChar { it.uppercase() }}",
            Exec::class
        ) {
            dependsOn(buildTaskName)
            environment("PATH", "/opt/homebrew/bin:" + System.getenv("PATH"))
            commandLine = listOf(
                cmakePath,
                "--build", cmakeBuildDir.absolutePath,
                "--target", "speech_static",
                "--verbose"
            )
        }

        val libPath = cmakeBuildDir.absolutePath
        val mergeTask = tasks.register(
            "mergeSpeechStatic${arch.name.replaceFirstChar { it.uppercase() }}",
            Exec::class
        ) {
            dependsOn(compileTask)
            doFirst {
                val libs = mutableListOf(
                    "$libPath/libspeech_static.a"
                )
                // Add whisper libs if they exist
                val whisperLib = "$libPath/whisper-build/src/libwhisper.a"
                if (file(whisperLib).exists()) libs.add(whisperLib)

                // Add ggml libs if they exist
                listOf(
                    "$libPath/whisper-build/ggml/src/libggml.a",
                    "$libPath/whisper-build/ggml/src/libggml-base.a",
                    "$libPath/whisper-build/ggml/src/libggml-cpu.a",
                    "$libPath/whisper-build/ggml/src/ggml-metal/libggml-metal.a",
                    "$libPath/whisper-build/ggml/src/ggml-blas/libggml-blas.a"
                ).forEach { if (file(it).exists()) libs.add(it) }

                // Add whisper.coreml lib if it exists (for Apple Neural Engine)
                val coremlLib = "$libPath/whisper-build/src/libwhisper.coreml.a"
                if (file(coremlLib).exists()) libs.add(coremlLib)

                // Add piper libs if they exist
                val piperLib = "$libPath/piper-build/libpiper.a"
                if (file(piperLib).exists()) libs.add(piperLib)

                commandLine = listOf(libtoolPath, "-static", "-o", "$libPath/libspeech_merged.a") + libs
            }
        }

        // Ensure cinterop runs after native libs are built
        tasks.withType<org.jetbrains.kotlin.gradle.tasks.CInteropProcess>().configureEach {
            dependsOn(mergeTask)
        }

        arch.compilations.getByName("main").cinterops {
            create("speech") {
                defFile("src/iosMain/c_interop/speech_ios.def")
                packageName("dev.deviceai.native")
                compilerOpts(
                    "-I${projectDir}/src/iosMain/c_interop/include",
                    "-I${rootDir}/cpp/speech",
                    "-I${rootDir}/whisper.cpp/include",
                    "-I${rootDir}/whisper.cpp"
                )
                extraOpts("-libraryPath", libPath)

                tasks.named(interopProcessingTaskName).configure {
                    dependsOn(mergeTask)
                }
            }
        }

        val merged = "$libPath/libspeech_merged.a"

        arch.binaries.getFramework("DEBUG").apply {
            baseName = "deviceai-runtime-kmp"
            isStatic = true
            linkerOpts(
                "-L$libPath",
                "-Wl,-force_load", merged,
                "-framework", "Accelerate",
                "-framework", "Metal",
                "-framework", "CoreML",
                "-Wl,-no_implicit_dylibs",
                if (sdkName.contains("Simulator"))
                    "-mios-simulator-version-min=$minIos"
                else
                    "-mios-version-min=$minIos"
            )
        }
        arch.binaries.getFramework("RELEASE").apply {
            baseName = "deviceai-runtime-kmp"
            isStatic = true
            linkerOpts(
                "-L$libPath",
                "-Wl,-force_load", merged,
                "-framework", "Accelerate",
                "-framework", "Metal",
                "-framework", "CoreML",
                "-Wl,-no_implicit_dylibs",
                if (sdkName.contains("Simulator"))
                    "-mios-simulator-version-min=$minIos"
                else
                    "-mios-version-min=$minIos"
            )
        }
    }

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
        iosMain.dependencies {
            implementation(libs.ktor.client.darwin)
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
    publishToMavenCentral(com.vanniktech.maven.publish.SonatypeHost.CENTRAL_PORTAL)
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
