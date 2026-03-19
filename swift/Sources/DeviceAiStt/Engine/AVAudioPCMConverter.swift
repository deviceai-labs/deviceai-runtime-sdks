import AVFoundation

/// Loads any audio file and resamples it to 16 kHz mono Float32 —
/// the exact format whisper.cpp expects.
enum AVAudioPCMConverter {

    static func loadMonoPCM(from url: URL) throws -> [Float] {
        let file   = try AVAudioFile(forReading: url)
        let format = AVAudioFormat(commonFormat: .pcmFormatFloat32,
                                   sampleRate: 16000,
                                   channels: 1,
                                   interleaved: false)!
        guard let converter = AVAudioConverter(from: file.processingFormat, to: format) else {
            throw AudioLoadError.conversionUnsupported
        }

        let frameCount = AVAudioFrameCount(Double(file.length) *
                                           (16000 / file.processingFormat.sampleRate)) + 1
        guard let output = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount) else {
            throw AudioLoadError.bufferAllocationFailed
        }

        var error: NSError?
        let inputBlock: AVAudioConverterInputBlock = { _, outStatus in
            let capacity = AVAudioFrameCount(file.length)
            guard let buf = AVAudioPCMBuffer(pcmFormat: file.processingFormat,
                                             frameCapacity: capacity) else {
                outStatus.pointee = .noDataNow; return nil
            }
            do {
                try file.read(into: buf)
                outStatus.pointee = buf.frameLength > 0 ? .haveData : .endOfStream
                return buf
            } catch {
                outStatus.pointee = .endOfStream; return nil
            }
        }

        converter.convert(to: output, error: &error, withInputFrom: inputBlock)
        if let e = error { throw e }

        let count = Int(output.frameLength)
        guard let channel = output.floatChannelData?[0] else { return [] }
        return Array(UnsafeBufferPointer(start: channel, count: count))
    }
}

private enum AudioLoadError: Error {
    case conversionUnsupported
    case bufferAllocationFailed
}
