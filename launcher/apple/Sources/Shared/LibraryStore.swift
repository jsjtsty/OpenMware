import Foundation
import SwiftUI

#if os(macOS)
import AppKit
#else
import UIKit
#endif

struct Access {
    let url: URL
    let stop: () -> Void
}

@MainActor
final class LibraryStore: ObservableObject {
    @Published private(set) var games: [Game] = []
    /// Id of the game that is running, if any.
    @Published private(set) var playing: String?
    @Published var lastError: String?

    private let root: URL
    private var index: URL { root.appendingPathComponent("library.json") }
    #if os(macOS)
    private var player: Process?
    private static let bookmarkCreate: URL.BookmarkCreationOptions = [.withSecurityScope]
    private static let bookmarkResolve: URL.BookmarkResolutionOptions = [.withSecurityScope]
    #else
    private static let bookmarkCreate: URL.BookmarkCreationOptions = []
    private static let bookmarkResolve: URL.BookmarkResolutionOptions = []
    #endif

    init() {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        root = support.appendingPathComponent("OpenMware", isDirectory: true)
        try? FileManager.default.createDirectory(at: root.appendingPathComponent("Games"), withIntermediateDirectories: true)
        load()
    }

    // MARK: Paths

    func folder(_ game: Game) -> URL { root.appendingPathComponent("Games/\(game.id)", isDirectory: true) }
    func keysURL(_ game: Game) -> URL { folder(game).appendingPathComponent("keys.txt") }
    func dataURL(_ game: Game) -> URL { folder(game).appendingPathComponent("data", isDirectory: true) }
    private func copiedURL(_ game: Game) -> URL { folder(game).appendingPathComponent("game", isDirectory: true) }

    /// The folder with the game's .npk files. Linked games need their security scope opened; call `stop` when done.
    func openGameFolder(_ game: Game) -> Access? {
        switch game.storage {
        case .copied:
            return Access(url: copiedURL(game), stop: {})
        case .linked(let bookmark):
            var stale = false
            guard let url = try? URL(resolvingBookmarkData: bookmark, options: Self.bookmarkResolve, relativeTo: nil, bookmarkDataIsStale: &stale) else { return nil }
            let opened = url.startAccessingSecurityScopedResource()
            if stale, let fresh = try? url.bookmarkData(options: Self.bookmarkCreate, includingResourceValuesForKeys: nil, relativeTo: nil),
               let i = games.firstIndex(where: { $0.id == game.id }) {
                games[i].storage = .linked(bookmark: fresh)
                save()
            }
            return Access(url: url, stop: { if opened { url.stopAccessingSecurityScopedResource() } })
        }
    }

    func status(_ game: Game) -> GameStatus {
        guard let access = openGameFolder(game) else { return .incomplete }
        defer { access.stop() }
        let script = access.url.appendingPathComponent("script.npk")
        if !FileManager.default.fileExists(atPath: script.path) { return .incomplete }
        return Keys.count(in: keysURL(game)) == 0 ? .keysMissing : .ready
    }

    func gameBytes(_ game: Game) -> (count: Int, bytes: Int64) {
        guard let access = openGameFolder(game) else { return (0, 0) }
        defer { access.stop() }
        let files = Self.archives(in: access.url)
        return (files.count, files.reduce(0) { $0 + Self.size($1) })
    }

    func saveBytes(_ game: Game) -> Int64 {
        let e = FileManager.default.enumerator(at: dataURL(game), includingPropertiesForKeys: [.fileSizeKey])
        var total: Int64 = 0
        while let url = e?.nextObject() as? URL { total += Self.size(url) }
        return total
    }

    func keyCount(_ game: Game) -> Int { Keys.count(in: keysURL(game)) }

    // MARK: Index

    private func load() {
        guard let data = try? Data(contentsOf: index), let list = try? JSONDecoder().decode([Game].self, from: data) else { return }
        games = list.filter { FileManager.default.fileExists(atPath: folder($0).path) }
    }

    private func save() {
        if let data = try? JSONEncoder().encode(games) { try? data.write(to: index, options: .atomic) }
    }

    private func update(_ id: String, _ change: (inout Game) -> Void) {
        guard let i = games.firstIndex(where: { $0.id == id }) else { return }
        change(&games[i])
        save()
    }

    // MARK: Adding

    nonisolated static func archives(in dir: URL) -> [URL] {
        let items = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey])) ?? []
        return items.filter { $0.pathExtension.lowercased() == "npk" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    nonisolated static func size(_ url: URL) -> Int64 {
        Int64((try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
    }

    /// Looks into a folder from the picker. The folder's security scope is opened only while listing.
    func scan(_ url: URL) -> FolderScan {
        let opened = url.startAccessingSecurityScopedResource()
        defer { if opened { url.stopAccessingSecurityScopedResource() } }
        let files = Self.archives(in: url)
        return FolderScan(url: url, displayName: url.lastPathComponent, archives: files, totalBytes: files.reduce(0) { $0 + Self.size($1) })
    }

    func freeBytes() -> Int64 {
        let values = try? root.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        return values?.volumeAvailableCapacityForImportantUsage ?? Int64.max
    }

    /// Adds a game. With `copy`, the archives are copied into the app (`progress` reports bytes); otherwise the folder is linked.
    func add(_ scan: FolderScan, name: String, keysText: String, copy: Bool, progress: @escaping @Sendable (Int64) -> Void) async throws -> Game {
        let id = String(UUID().uuidString.prefix(8)).lowercased()
        var game = Game(id: id, name: name.isEmpty ? scan.displayName : name, addedAt: Date(), lastPlayedAt: nil, playSeconds: 0, storage: .copied)
        let dir = folder(game)
        let fm = FileManager.default
        try fm.createDirectory(at: dataURL(game), withIntermediateDirectories: true)
        do {
            if copy {
                let target = copiedURL(game)
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
                let files = scan.archives
                let source = scan.url
                try await Task.detached {
                    let opened = source.startAccessingSecurityScopedResource()
                    defer { if opened { source.stopAccessingSecurityScopedResource() } }
                    var done: Int64 = 0
                    for file in files {
                        try Task.checkCancellation()
                        done = try Self.copyFile(file, to: target.appendingPathComponent(file.lastPathComponent), done: done, progress: progress)
                    }
                }.value
                game.storage = .copied
            } else {
                let opened = scan.url.startAccessingSecurityScopedResource()
                defer { if opened { scan.url.stopAccessingSecurityScopedResource() } }
                let bookmark = try scan.url.bookmarkData(options: Self.bookmarkCreate, includingResourceValuesForKeys: nil, relativeTo: nil)
                game.storage = .linked(bookmark: bookmark)
            }
            let keys = Keys.parse(keysText)
            if !keys.isEmpty { try (keys.joined(separator: "\n") + "\n").write(to: keysURL(game), atomically: true, encoding: .utf8) }
        } catch {
            try? fm.removeItem(at: dir)
            throw error
        }
        games.append(game)
        save()
        return game
    }

    nonisolated private static func copyFile(_ from: URL, to: URL, done: Int64, progress: @Sendable (Int64) -> Void) throws -> Int64 {
        let fm = FileManager.default
        try? fm.removeItem(at: to)
        fm.createFile(atPath: to.path, contents: nil)
        let input = try FileHandle(forReadingFrom: from)
        let output = try FileHandle(forWritingTo: to)
        defer { try? input.close(); try? output.close() }
        var total = done
        while true {
            try Task.checkCancellation()
            guard let chunk = try input.read(upToCount: 8 << 20), !chunk.isEmpty else { break }
            try output.write(contentsOf: chunk)
            total += Int64(chunk.count)
            progress(total)
        }
        return total
    }

    func readText(_ url: URL) -> String {
        let opened = url.startAccessingSecurityScopedResource()
        defer { if opened { url.stopAccessingSecurityScopedResource() } }
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    // MARK: Editing

    func rename(_ game: Game, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        if !trimmed.isEmpty { update(game.id) { $0.name = trimmed } }
    }

    @discardableResult
    func setKeys(_ game: Game, text: String) -> Int {
        let keys = Keys.parse(text)
        if !keys.isEmpty { try? (keys.joined(separator: "\n") + "\n").write(to: keysURL(game), atomically: true, encoding: .utf8) }
        objectWillChange.send()
        return keys.count
    }

    func remove(_ game: Game) {
        try? FileManager.default.removeItem(at: folder(game))
        games.removeAll { $0.id == game.id }
        save()
    }

    func clearSaves(_ game: Game) {
        try? FileManager.default.removeItem(at: dataURL(game))
        try? FileManager.default.createDirectory(at: dataURL(game), withIntermediateDirectories: true)
        objectWillChange.send()
    }

    /// A zip of the save folder in the temporary directory (for sharing / saving).
    func exportSaves(_ game: Game) -> URL? {
        var coordinationError: NSError?
        var result: URL?
        let safe = game.name.components(separatedBy: CharacterSet.alphanumerics.inverted).filter { !$0.isEmpty }.joined(separator: "_")
        NSFileCoordinator().coordinate(readingItemAt: dataURL(game), options: .forUploading, error: &coordinationError) { zip in
            let dest = FileManager.default.temporaryDirectory.appendingPathComponent("\(safe.isEmpty ? "game" : safe)-save.zip")
            try? FileManager.default.removeItem(at: dest)
            if (try? FileManager.default.copyItem(at: zip, to: dest)) != nil { result = dest }
        }
        return result
    }

    // MARK: Playing

    private func markPlayStarted(_ game: Game) { update(game.id) { $0.lastPlayedAt = Date() } }
    private func addPlayTime(_ id: String, _ seconds: Double) { update(id) { $0.playSeconds += seconds } }

    private var shadersURL: URL { (Bundle.main.resourceURL ?? Bundle.main.bundleURL).appendingPathComponent("shaders", isDirectory: true) }

    func stop() {
        #if os(macOS)
        player?.terminate()
        #endif
    }

    func play(_ game: Game) {
        guard playing == nil else { return }
        guard let access = openGameFolder(game) else {
            lastError = L("error_folder_unavailable")
            return
        }
        playing = game.id
        markPlayStarted(game)
        let started = Date()
        let keys = keysURL(game), data = dataURL(game), shaders = shadersURL
        #if os(macOS)
        let log = folder(game).appendingPathComponent("last-run.log")
        FileManager.default.createFile(atPath: log.path, contents: nil)
        do {
            player = try Engine.launchPlayer(gameDir: access.url, keys: keys, dataDir: data, shaders: shaders, log: log) { [weak self] status in
                access.stop()
                guard let self else { return }
                self.player = nil
                self.playing = nil
                self.addPlayTime(game.id, Date().timeIntervalSince(started))
                NSApp.unhide(nil)
                NSApp.activate(ignoringOtherApps: true)
                if status != 0 && status != 15 { // 15: stopped from the launcher
                    self.lastError = L("error_player_exit", Int(status)) + "\n" + ((try? String(contentsOf: log, encoding: .utf8))?.suffix(400).description ?? "")
                }
            }
            // The launcher steps aside while the game runs and comes back when it ends (above). The short wait lets the
            // game's window appear first, and skips hiding when it fails at once.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) { [weak self] in
                if self?.playing == game.id { NSApp.hide(nil) }
            }
        } catch {
            access.stop()
            playing = nil
            lastError = error.localizedDescription
        }
        #else
        // The engine runs in this process and blocks the main thread until the game window is closed (SDL pumps the
        // UIKit run loop meanwhile). Hop out of the current SwiftUI update first.
        DispatchQueue.main.async { [weak self] in
            let error = Engine.run(gameDir: access.url, keys: keys, dataDir: data, shaders: shaders)
            access.stop()
            guard let self else { return }
            self.playing = nil
            self.addPlayTime(game.id, Date().timeIntervalSince(started))
            Engine.restoreLauncherWindow()
            if let error { self.lastError = error }
        }
        #endif
    }
}
