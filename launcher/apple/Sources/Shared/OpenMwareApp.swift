import SwiftUI

@main
struct OpenMwareApp: App {
    @StateObject private var store = LibraryStore()

    var body: some Scene {
        WindowGroup {
            #if os(macOS)
            MacLibraryView()
                .environmentObject(store)
                .frame(minWidth: 760, minHeight: 520)
            #else
            LibraryView()
                .environmentObject(store)
            #endif
        }
        #if os(macOS)
        .windowToolbarStyle(.unified)
        .defaultSize(width: 980, height: 660)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
        #endif
    }
}
