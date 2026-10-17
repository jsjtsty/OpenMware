import Foundation

/// Localized string with optional format arguments (Localizable.strings, English and Simplified Chinese).
func L(_ key: String, _ args: CVarArg...) -> String {
    let format = NSLocalizedString(key, comment: "")
    return args.isEmpty ? format : String(format: format, arguments: args)
}

func formatBytes(_ bytes: Int64) -> String {
    ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)
}

func formatDuration(_ seconds: Double) -> String {
    let minutes = Int(seconds / 60)
    return minutes < 60 ? L("minutes_short", minutes) : L("hours_short", minutes / 60, minutes % 60)
}
