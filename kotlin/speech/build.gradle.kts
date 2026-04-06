import com.android.build.api.dsl.LibraryExtension

plugins {
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.kotlinx.serialization)
    id("com.android.library")
    alias(libs.plugins.vanniktech.publish)
}

group = "dev.deviceai"
version = (System.getenv("RELEASE_VERSION") ?: "0.3.0-alpha01")

kotlin {
    jvmToolchain(17)

    androidTarget {
        publishLibraryVariants("release")
    }

    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlin.stdlib)
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
    }
}

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

    sourceSets {
        getByName("main") {
            // sherpa-onnx pre-built .so files for Android
            jniLibs.srcDirs("../../sdk/deviceai-commons/prebuilt/sherpa-onnx")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../sdk/deviceai-commons/CMakeLists.txt")
        }
    }

    defaultConfig {
        externalNativeBuild {
            cmake {
                arguments(
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DDAI_BUILD_JNI=ON",
                    "-DDAI_BUILD_IOS=OFF",
                    "-DDAI_ENABLE_STT=ON",
                    "-DDAI_ENABLE_TTS=ON",
                    "-DDAI_ENABLE_LLM=OFF",
                    "-DDAI_ENABLE_CORE=OFF"
                )
                targets("speech_jni")
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
        description.set("Android library for on-device Speech-to-Text and Text-to-Speech")
        url.set("https://github.com/deviceai-labs/deviceai")
        licenses {
            license {
                name.set("Apache-2.0")
                url.set("https://www.apache.org/licenses/LICENSE-2.0")
            }
        }
        developers {
            developer {
                id.set("NikhilBhutani")
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
