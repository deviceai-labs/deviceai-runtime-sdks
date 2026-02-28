import com.android.build.api.dsl.LibraryExtension

plugins {
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.kotlinx.serialization)
    id("com.android.library")
    alias(libs.plugins.vanniktech.publish)
}

group = "dev.deviceai"
version = (System.getenv("RELEASE_VERSION") ?: "0.1.0-SNAPSHOT")

val minIos = "17.0"

kotlin {
    jvmToolchain(17)

    androidTarget {
        publishLibraryVariants("release")
    }

    jvm()

    iosX64()
    iosArm64()
    iosSimulatorArm64()

    targets.withType<org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget> {
        binaries.framework {
            baseName = "deviceai-runtime-llm"
            isStatic = true
            linkerOpts("-Wl,-no_implicit_dylibs")
            freeCompilerArgs += listOf("-Xbinary=bundleId=dev.deviceai.llm")
            freeCompilerArgs += "-Xoverride-konan-properties=osVersionMin.ios=$minIos"
        }
    }

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

    val cmakePath   = findTool("cmake")
    val libtoolPath = findTool("libtool")

    // iOS native builds
    listOf(
        Triple(iosX64(),              "x86_64", "iPhoneSimulator"),
        Triple(iosArm64(),            "arm64",  "iPhoneOS"),
        Triple(iosSimulatorArm64(),   "arm64",  "iPhoneSimulator")
    ).forEach { (arch, archName, sdkName) ->
        val cmakeBuildDir = layout.buildDirectory
            .dir("llm-cmake/$sdkName/${arch.name}")
            .get().asFile
        val buildTaskName = "buildLlmCMake${arch.name.replaceFirstChar { it.uppercase() }}"

        tasks.register(buildTaskName, Exec::class) {
            doFirst {
                val sourceDir = projectDir.resolve("cmake/llm-wrapper-ios")
                val sdk = if (sdkName == "iPhoneOS") "iphoneos" else "iphonesimulator"
                val sdkPath = providers.exec {
                    commandLine("xcrun", "--sdk", sdk, "--show-sdk-path")
                }.standardOutput.asText.map { it.trim() }
                cmakeBuildDir.mkdirs()
                environment("PATH", "/opt/homebrew/bin:" + System.getenv("PATH"))
                commandLine = listOf(
                    cmakePath,
                    "-S", sourceDir.absolutePath,
                    "-B", cmakeBuildDir.absolutePath,
                    "-DCMAKE_SYSTEM_NAME=iOS",
                    "-DCMAKE_OSX_ARCHITECTURES=$archName",
                    "-DCMAKE_OSX_SYSROOT=${sdkPath.get()}",
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$minIos",
                    "-DCMAKE_INSTALL_PREFIX=${cmakeBuildDir.resolve("install")}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
                )
            }
        }

        val compileTask = tasks.register(
            "compileLlmCMake${arch.name.replaceFirstChar { it.uppercase() }}", Exec::class
        ) {
            dependsOn(buildTaskName)
            environment("PATH", "/opt/homebrew/bin:" + System.getenv("PATH"))
            commandLine = listOf(cmakePath, "--build", cmakeBuildDir.absolutePath, "--target", "llm_static", "--verbose")
        }

        val libPath = cmakeBuildDir.absolutePath
        val mergeTask = tasks.register(
            "mergeLlmStatic${arch.name.replaceFirstChar { it.uppercase() }}", Exec::class
        ) {
            dependsOn(compileTask)
            doFirst {
                val libs = mutableListOf("$libPath/libllm_static.a")
                listOf(
                    "$libPath/llama-build/src/libllama.a",
                    "$libPath/llama-build/ggml/src/libggml.a",
                    "$libPath/llama-build/ggml/src/libggml-base.a",
                    "$libPath/llama-build/ggml/src/libggml-cpu.a",
                    "$libPath/llama-build/ggml/src/ggml-metal/libggml-metal.a",
                    "$libPath/llama-build/ggml/src/ggml-blas/libggml-blas.a"
                ).forEach { if (file(it).exists()) libs.add(it) }
                commandLine = listOf(libtoolPath, "-static", "-o", "$libPath/libllm_merged.a") + libs
            }
        }

        tasks.withType<org.jetbrains.kotlin.gradle.tasks.CInteropProcess>().configureEach {
            dependsOn(mergeTask)
        }

        arch.compilations.getByName("main").cinterops {
            create("llm") {
                defFile("src/iosMain/c_interop/llm_ios.def")
                packageName("dev.deviceai.llm.native")
                compilerOpts("-I${projectDir}/src/iosMain/c_interop/include")
                extraOpts("-libraryPath", libPath)
                tasks.named(interopProcessingTaskName).configure { dependsOn(mergeTask) }
            }
        }

        val merged = "$libPath/libllm_merged.a"
        listOf("DEBUG", "RELEASE").forEach { variant ->
            arch.binaries.getFramework(variant).apply {
                baseName = "deviceai-runtime-llm"
                isStatic = true
                linkerOpts(
                    "-L$libPath",
                    "-Wl,-force_load", merged,
                    "-framework", "Accelerate",
                    "-framework", "Metal",
                    "-framework", "MetalKit",
                    "-Wl,-no_implicit_dylibs",
                    if (sdkName.contains("Simulator")) "-mios-simulator-version-min=$minIos"
                    else "-mios-version-min=$minIos"
                )
            }
        }
    }

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
    publishToMavenCentral(com.vanniktech.maven.publish.SonatypeHost.CENTRAL_PORTAL)
    signAllPublications()

    coordinates("dev.deviceai", "runtime-llm", version.toString())

    pom {
        name.set("DeviceAI Runtime — LLM")
        description.set("Kotlin Multiplatform library for on-device LLM inference via llama.cpp")
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
