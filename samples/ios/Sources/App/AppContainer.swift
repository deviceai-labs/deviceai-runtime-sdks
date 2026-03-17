import SwiftUI
import DeviceAiCore

@Observable
@MainActor
final class AppContainer {
    let speechVM: SpeechViewModel
    let chatVM: ChatViewModel
    let modelsVM: ModelsViewModel

    init() {
        DeviceAI.configure()
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
