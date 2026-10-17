import SwiftUI

@main
struct OpenMwareApp: App {
    @StateObject private var store = LibraryStore()

    var body: some Scene {
        WindowGroup {
            LibraryView()
                .environmentObject(store)
                #if os(macOS)
                .frame(minWidth: 640, minHeight: 460)
                #endif
        }
        #if os(macOS)
        .defaultSize(width: 900, height: 620)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
        #endif
    }
}
