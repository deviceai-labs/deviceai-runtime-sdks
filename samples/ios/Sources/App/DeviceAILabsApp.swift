import SwiftUI
import UIKit
import DeviceAiCore

@main
struct DeviceAILabsApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var delegate

    // WindowGroup is required by the App protocol but the SceneDelegate
    // takes over and sets up the real UIWindow + UIHostingController.
    var body: some Scene {
        WindowGroup { EmptyView() }
    }
}

// MARK: - AppDelegate

final class AppDelegate: NSObject, UIApplicationDelegate {

    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        // Configure the SDK here — the earliest guaranteed synchronous callback,
        // before any scene / view is created.
        DeviceAI.configure()
        return true
    }

    func application(
        _ application: UIApplication,
        configurationForConnecting connectingSceneSession: UISceneSession,
        options: UIScene.ConnectionOptions
    ) -> UISceneConfiguration {
        let config = UISceneConfiguration(name: nil, sessionRole: connectingSceneSession.role)
        config.delegateClass = SceneDelegate.self
        return config
    }
}

// MARK: - SceneDelegate

final class SceneDelegate: NSObject, UIWindowSceneDelegate {
    var window: UIWindow?
    // Keep a strong reference so AppContainer isn't deallocated.
    var container: AppContainer?

    func scene(
        _ scene: UIScene,
        willConnectTo session: UISceneSession,
        options connectionOptions: UIScene.ConnectionOptions
    ) {
        guard let windowScene = scene as? UIWindowScene else { return }

        let c = AppContainer()
        container = c

        let rootView = MainView(
            speechVM: c.speechVM,
            chatVM:   c.chatVM,
            modelsVM: c.modelsVM
        )
        .preferredColorScheme(.dark)

        let hostingController = UIHostingController(rootView: rootView)
        hostingController.view.backgroundColor = .black

        let w = UIWindow(windowScene: windowScene)
        w.backgroundColor = .black
        w.rootViewController = hostingController
        w.makeKeyAndVisible()
        window = w
    }
}
