import SwiftUI

struct ChatView: View {
    @Bindable var viewModel: ChatViewModel
    var onClear: () -> Void = {}

    var body: some View {
        Group {
            if viewModel.modelPath == nil {
                noModelView
            } else {
                mainContent
            }
        }
    }

    private var noModelView: some View {
        VStack(spacing: 16) {
            Image(systemName: "cpu")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No LLM model selected")
                .font(.headline)
            Text("Tap the \u{24B8} icon above to download a model for on-device chat.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(40)
    }

    private var mainContent: some View {
        VStack(spacing: 0) {
            messageList
            Divider()
            inputBar
        }
    }

    // MARK: - Message list

    private var messageList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 12) {
                    ForEach(viewModel.messages) { msg in
                        MessageBubble(message: msg)
                            .id(msg.id)
                    }

                    // Live streaming bubble
                    if viewModel.isGenerating {
                        MessageBubble(
                            message: ChatMessage(
                                role: .assistant,
                                text: viewModel.streamingText.isEmpty ? "…" : viewModel.streamingText
                            )
                        )
                        .id("streaming")
                    }

                    if let error = viewModel.errorMessage {
                        Text(error)
                            .font(.caption)
                            .foregroundStyle(.red.opacity(0.8))
                            .padding(.horizontal)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
            }
            .onChange(of: viewModel.streamingText) { _, _ in
                withAnimation { proxy.scrollTo("streaming", anchor: .bottom) }
            }
            .onChange(of: viewModel.messages.count) { _, _ in
                if let last = viewModel.messages.last {
                    withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                }
            }
        }
    }

    // MARK: - Input bar

    private var inputBar: some View {
        HStack(spacing: 10) {
            TextField("Message…", text: $viewModel.inputText, axis: .vertical)
                .lineLimit(1...5)
                .padding(12)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))

            if viewModel.isGenerating {
                Button { viewModel.cancelTapped() } label: {
                    Image(systemName: "stop.circle.fill")
                        .font(.title2)
                        .foregroundStyle(.red.opacity(0.85))
                }
            } else {
                Button { viewModel.sendTapped() } label: {
                    Image(systemName: "arrow.up.circle.fill")
                        .font(.title2)
                        .foregroundStyle(
                            viewModel.inputText.trimmingCharacters(in: .whitespaces).isEmpty
                                ? AppTheme.accent.opacity(0.35)
                                : AppTheme.accent
                        )
                }
                .disabled(viewModel.inputText.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.ultraThinMaterial)
    }
}

// MARK: - Bubble

private struct MessageBubble: View {
    let message: ChatMessage

    private var isUser: Bool { message.role == .user }

    var body: some View {
        HStack {
            if isUser { Spacer(minLength: 50) }

            Text(message.text)
                .font(.body)
                .foregroundStyle(isUser ? .white : .primary)
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .background(
                    isUser
                        ? AnyShapeStyle(AppTheme.accent)
                        : AnyShapeStyle(.ultraThinMaterial),
                    in: RoundedRectangle(cornerRadius: 18)
                )
                .liquidGlassBubbleIfAvailable(isUser: isUser)

            if !isUser { Spacer(minLength: 50) }
        }
    }
}
