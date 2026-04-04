package dev.deviceai.core

/**
 * Marks a DeviceAI SDK API that is **internal to the SDK implementation**.
 *
 * These APIs exist to allow communication between SDK modules (e.g. `kotlin/llm`,
 * `kotlin/speech`) that live in separate Gradle modules and therefore cannot use
 * Kotlin's `internal` visibility modifier across module boundaries.
 *
 * **Do not use these APIs in application code.** They are not covered by any
 * compatibility guarantees and may change or disappear in any release without notice.
 *
 * If you are building a DeviceAI module (not an app), opt in with:
 * ```kotlin
 * @OptIn(InternalDeviceAiApi::class)
 * ```
 * or in your Gradle build:
 * ```kotlin
 * tasks.withType<KotlinCompile>().configureEach {
 *     compilerOptions.freeCompilerArgs.add("-opt-in=dev.deviceai.core.InternalDeviceAiApi")
 * }
 * ```
 */
@RequiresOptIn(
    message = "This is an internal DeviceAI SDK API. Do not use in application code — " +
              "it may change or be removed without notice.",
    level = RequiresOptIn.Level.ERROR,
)
@Retention(AnnotationRetention.BINARY)
@Target(
    AnnotationTarget.CLASS,
    AnnotationTarget.FUNCTION,
    AnnotationTarget.PROPERTY,
    AnnotationTarget.CONSTRUCTOR,
)
annotation class InternalDeviceAiApi
