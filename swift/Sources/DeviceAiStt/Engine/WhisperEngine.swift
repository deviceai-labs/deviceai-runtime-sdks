import Foundation
import DeviceAiCore
import CWhisper

/// Internal Whisper inference driver.
/// All calls must be serialized by the owning `SttSession` actor.
final class WhisperEngine: Transcribing, @unchecked Sendable {

    private let queue = DispatchQueue(label: "dev.deviceai.stt.whisper", qos: .userInitiated)
    private var ctx: OpaquePointer?
    private var isCancelled = false
    private var isClosed    = false

    // MARK: - Lifecycle

    func load(modelPath: String) async throws {
        guard !isClosed else { throw DeviceAiError.sessionClosed }
        guard FileManager.default.fileExists(atPath: modelPath) else {
            throw DeviceAiError.modelNotFound(path: modelPath)
        }
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            queue.async {
                guard let c = whisper_init_from_file(modelPath) else {
                    cont.resume(throwing: DeviceAiError.modelLoadFailed(reason: "whisper_init_from_file returned nil"))
                    return
                }
                self.ctx = c
                DeviceAiLogger.info("WhisperEngine", "Model loaded: \(modelPath)")
                cont.resume()
            }
        }
    }

    func close() async {
        await withCheckedContinuation { (cont: CheckedContinuation<Void, Never>) in
            queue.async {
                if let c = self.ctx { whisper_free(c); self.ctx = nil }
                self.isClosed = true
                cont.resume()
            }
        }
    }

    func cancel() { isCancelled = true }

    // MARK: - Transcription (raw samples)

    func transcribe(samples: [Float], config: TranscriptionConfig) async throws -> TranscriptionResult {
        try await withCheckedThrowingContinuation { cont in
            queue.async {
                self.isCancelled = false
                guard let ctx = self.ctx else {
                    cont.resume(throwing: DeviceAiError.notInitialised); return
                }
                var params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY)
                params.language          = (config.language ?? "auto").withCString { $0 }
                params.translate         = false
                params.no_context        = true
                params.single_segment    = false
                params.print_progress    = false
                params.print_realtime    = false
                params.print_timestamps  = false

                let rc = samples.withUnsafeBufferPointer { buf in
                    whisper_full(ctx, params, buf.baseAddress, Int32(samples.count))
                }
                guard rc == 0 else {
                    cont.resume(throwing: DeviceAiError.inferenceFailed(reason: "whisper_full rc=\(rc)"))
                    return
                }
                cont.resume(returning: Self.collectResult(ctx: ctx))
            }
        }
    }

    func transcribe(audioPath: String, config: TranscriptionConfig) async throws -> TranscriptionResult {
        // Load WAV → PCM via AVFoundation
        let samples = try AVAudioPCMConverter.loadMonoPCM(from: URL(fileURLWithPath: audioPath))
        return try await transcribe(samples: samples, config: config)
    }

    // MARK: - Streaming (segment-by-segment)

    func transcribeStream(samples: [Float], config: TranscriptionConfig) -> AsyncThrowingStream<String, Error> {
        AsyncThrowingStream { continuation in
            queue.async {
                self.isCancelled = false
                guard let ctx = self.ctx else {
                    continuation.finish(throwing: DeviceAiError.notInitialised); return
                }
                var params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY)
                params.language         = (config.language ?? "auto").withCString { $0 }
                params.translate        = false
                params.no_context       = true
                params.print_progress   = false
                params.print_realtime   = false
                params.print_timestamps = false

                let rc = samples.withUnsafeBufferPointer { buf in
                    whisper_full(ctx, params, buf.baseAddress, Int32(samples.count))
                }
                guard rc == 0 else {
                    continuation.finish(throwing: DeviceAiError.inferenceFailed(reason: "whisper_full rc=\(rc)"))
                    return
                }
                let n = whisper_full_n_segments(ctx)
                for i in 0 ..< n {
                    guard !self.isCancelled else {
                        continuation.finish(throwing: DeviceAiError.cancelled); return
                    }
                    if let ptr = whisper_full_get_segment_text(ctx, i) {
                        continuation.yield(String(cString: ptr))
                    }
                }
                continuation.finish()
            }
        }
    }

    // MARK: - Helpers

    private static func collectResult(ctx: OpaquePointer) -> TranscriptionResult {
        let n = whisper_full_n_segments(ctx)
        var fullText = ""
        var segments: [TranscriptionSegment] = []
        for i in 0 ..< n {
            let text = whisper_full_get_segment_text(ctx, i).map { String(cString: $0) } ?? ""
            let t0   = whisper_full_get_segment_t0(ctx, i)   // centiseconds
            let t1   = whisper_full_get_segment_t1(ctx, i)
            fullText += text
            segments.append(TranscriptionSegment(
                text:    text.trimmingCharacters(in: .whitespaces),
                startMs: t0 * 10,
                endMs:   t1 * 10
            ))
        }
        let lang = whisper_full_lang_id(ctx)
        let langStr = lang >= 0 ? (whisper_lang_str(lang).map { String(cString: $0) } ?? "en") : "en"
        return TranscriptionResult(
            text:      fullText.trimmingCharacters(in: .whitespaces),
            segments:  segments,
            language:  langStr,
            durationMs: segments.last?.endMs ?? 0
        )
    }

}

// MARK: - Convenience

private extension TranscriptionResult {
    static let empty = TranscriptionResult(text: "", segments: [], language: "en", durationMs: 0)
}
