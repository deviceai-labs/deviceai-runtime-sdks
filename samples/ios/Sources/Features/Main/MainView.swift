import SwiftUI

struct MainView: View {
    @State private var selectedTab: AppTab = .speech
    @State private var isShowingModels = false

    let speechVM: SpeechViewModel
    let chatVM: ChatViewModel
    let modelsVM: ModelsViewModel

    enum AppTab { case speech, chat }

    var body: some View {
        VStack(spacing: 0) {
            navBar

            Group {
                if selectedTab == .speech {
                    SpeechView(viewModel: speechVM)
                } else {
                    ChatView(viewModel: chatVM)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            CustomTabBar(selectedTab: $selectedTab)
                // Extend tab bar background into the home indicator safe area
                .ignoresSafeArea(.all, edges: .bottom)
        }
        .background(Color.black)
        .sheet(isPresented: $isShowingModels) {
            ModelsView(viewModel: modelsVM)
                .preferredColorScheme(.dark)
        }
    }

    private var navBar: some View {
        HStack {
            Text(selectedTab == .speech ? "Transcribe" : "Chat")
                .font(.headline)
                .foregroundStyle(.white)

            Spacer()

            if selectedTab == .speech {
                Button("Clear") { speechVM.clearTapped() }
                    .foregroundStyle(AppTheme.accent)
                    .disabled(speechVM.transcript.isEmpty && speechVM.errorMessage == nil)
            } else {
                Button("Clear") { chatVM.clearTapped() }
                    .foregroundStyle(AppTheme.accent)
                    .disabled(chatVM.messages.isEmpty && !chatVM.isGenerating)
            }

            Button {
                modelsVM.loadIfNeeded()
                isShowingModels = true
            } label: {
                Image(systemName: "cpu")
                    .foregroundStyle(AppTheme.accent)
            }
            .padding(.leading, 8)
        }
        .padding(.horizontal, 16)
        .frame(height: 44)
        .background(Color.black)
        .overlay(alignment: .bottom) {
            Rectangle().fill(Color.white.opacity(0.12)).frame(height: 0.5)
        }
    }
}

// MARK: - Custom Tab Bar

private struct CustomTabBar: View {
    @Binding var selectedTab: MainView.AppTab

    var body: some View {
        HStack(spacing: 0) {
            TabBarButton(
                title: "Speech",
                icon: selectedTab == .speech ? "mic.fill" : "mic",
                isSelected: selectedTab == .speech
            ) { selectedTab = .speech }

            TabBarButton(
                title: "Chat",
                icon: selectedTab == .chat
                    ? "bubble.left.and.bubble.right.fill"
                    : "bubble.left.and.bubble.right",
                isSelected: selectedTab == .chat
            ) { selectedTab = .chat }
        }
        .frame(maxWidth: .infinity)
        .frame(height: 49)
        .padding(.bottom, UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .first?.windows.first?.safeAreaInsets.bottom ?? 0)
        .background(Color.black)
        .overlay(alignment: .top) {
            Rectangle().fill(Color.white.opacity(0.12)).frame(height: 0.5)
        }
    }
}

private struct TabBarButton: View {
    let title: String
    let icon: String
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 3) {
                Image(systemName: icon)
                    .font(.system(size: 22))
                Text(title)
                    .font(.caption2.weight(.medium))
            }
            .foregroundStyle(isSelected ? AppTheme.accent : Color.white.opacity(0.4))
            .frame(maxWidth: .infinity)
        }
        .buttonStyle(.plain)
    }
}
