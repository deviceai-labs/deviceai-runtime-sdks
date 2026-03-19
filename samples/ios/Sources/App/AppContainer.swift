import SwiftUI
import UIKit
import DeviceAiCore

@Observable
@MainActor
final class AppContainer {
    let speechVM: SpeechViewModel
    let chatVM: ChatViewModel
    let modelsVM: ModelsViewModel

    init() {
        // Set window background so status bar + home indicator areas match theme
        UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap { $0.windows }
            .forEach { $0.backgroundColor = .black }

        speechVM = SpeechViewModel(speech: LiveSpeechRepository())
        chatVM   = ChatViewModel(chat: LiveChatRepository())
        modelsVM = ModelsViewModel(models: LiveModelRepository())

        // Wire model selection callbacks.
        // Use local lets so weak captures don't need to reference stored `let` properties directly.
        let s = speechVM
        let c = chatVM
        modelsVM.onSttSelected = { [weak s] path in s?.modelPath = path }
        modelsVM.onLlmSelected = { [weak c] path in c?.modelPath = path }
    }
}
