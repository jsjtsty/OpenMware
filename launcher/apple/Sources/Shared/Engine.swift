import Foundation

#if os(iOS)
import UIKit
#endif

struct ProbeResult {
    var ok: Bool
    var archives: Int
    var message: String
}

/// The engine, reached through libmware (include/mware/mware.h). On macOS the game itself runs in the bundled
/// `openmware-player` process; on iOS there are no child processes, so the library plays in this process.
enum Engine {
    static var version: String { String(cString: mw_version()) }

    /// Opens the archives with the keys to see whether they are good, without starting the game.
    static func probe(gameDir: URL, keys: URL) -> ProbeResult {
        var r = mw_probe_result()
        let ok = mw_probe(gameDir.path, keys.path, &r) != 0
        let message = withUnsafeBytes(of: &r.message) { raw -> String in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
        return ProbeResult(ok: ok, archives: Int(r.archives), message: message)
    }

    #if os(macOS)
    static func launchPlayer(gameDir: URL, keys: URL, dataDir: URL, shaders: URL, log: URL,
                             onExit: @escaping @MainActor (Int32) -> Void) throws -> Process {
        guard let exe = Bundle.main.executableURL?.deletingLastPathComponent().appendingPathComponent("openmware-player") else {
            throw CocoaError(.fileNoSuchFile)
        }
        let process = Process()
        process.executableURL = exe
        process.arguments = [gameDir.path, "--keys", keys.path, "--data-dir", dataDir.path, "--shaders", shaders.path]
        if let handle = try? FileHandle(forWritingTo: log) {
            process.standardOutput = handle
            process.standardError = handle
        }
        process.terminationHandler = { finished in
            let status = finished.terminationStatus
            Task { @MainActor in onExit(status) }
        }
        try process.run()
        return process
    }
    #else
    /// Plays in-process; returns an error message, or nil when the game ended normally.
    static func run(gameDir: URL, keys: URL, dataDir: URL, shaders: URL) -> String? {
        var error = [CChar](repeating: 0, count: 512)
        let code = gameDir.path.withCString { g in
            keys.path.withCString { k in
                dataDir.path.withCString { d in
                    shaders.path.withCString { s in
                        var options = mw_options(game_dir: g, keys_file: k, data_dir: d, shaders_dir: s, window_title: nil)
                        return mw_run(&options, &error, error.count)
                    }
                }
            }
        }
        return code == 0 ? nil : String(cString: error)
    }

    /// SDL's window is gone after a game; make the SwiftUI window the key window again.
    static func restoreLauncherWindow() {
        for scene in UIApplication.shared.connectedScenes {
            guard let windowScene = scene as? UIWindowScene else { continue }
            let launcher = windowScene.windows.first { window in
                guard let root = window.rootViewController else { return false }
                return !String(describing: type(of: root)).contains("SDL")
            }
            if let launcher {
                launcher.makeKeyAndVisible()
                return
            }
        }
    }
    #endif
}
