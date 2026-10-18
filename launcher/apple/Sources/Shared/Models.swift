import Foundation

/// How the game's files are reached: copied into the app's own storage, linked to the folder the user picked
/// (a security-scoped bookmark, so nothing is duplicated), or (iOS) a folder the user put into the app's Documents folder
/// with the Files app; those are found by `LibraryStore.syncDocuments()` and stored by folder name.
enum GameStorage: Codable, Equatable {
    case copied
    case linked(bookmark: Data)
    case documents(folder: String)
}

/// One game of the library. App-owned files live in Application Support/OpenMware/Games/<id>/ :
/// game/ (when copied), keys.txt, data/ (saves).
struct Game: Identifiable, Codable, Equatable {
    var id: String
    var name: String
    var addedAt: Date
    var lastPlayedAt: Date?
    var playSeconds: Double
    var storage: GameStorage

    /// Hue (0...1) of the generated cover, stable per game.
    var coverHue: Double {
        var h: UInt32 = 2166136261
        for b in id.utf8 { h = (h ^ UInt32(b)) &* 16777619 }
        return Double(h % 360) / 360.0
    }
}

enum GameStatus {
    case ready
    case keysMissing
    case incomplete
}

/// The NPK keys file: one 256-bit key (64 hex digits) per line.
enum Keys {
    static func parse(_ text: String) -> [String] {
        text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { $0.count == 64 && $0.allSatisfy { $0.isHexDigit } }
            .map { $0.lowercased() }
    }

    static func count(in url: URL) -> Int {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return 0 }
        return parse(text).count
    }
}

/// A folder picked by the user, looked at but not yet added.
struct FolderScan {
    var url: URL
    var displayName: String
    var archives: [URL]
    var totalBytes: Int64
    var isGame: Bool { archives.contains { $0.lastPathComponent.lowercased() == "script.npk" } }
}

extension GameStorage {
    var labelKey: String {
        switch self {
        case .copied: return "storage_copied"
        case .linked: return "storage_linked"
        case .documents: return "storage_documents"
        }
    }

    var icon: String {
        switch self {
        case .copied: return "square.on.square"
        case .linked: return "link"
        case .documents: return "folder"
        }
    }
}

extension GameStorage {
    /// The folder name in Documents, for a game found there.
    var documentsFolder: String? {
        if case .documents(let name) = self { return name }
        return nil
    }
}
