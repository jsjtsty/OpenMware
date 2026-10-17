import SwiftUI

/// A generated cover (the engine ships no artwork): a soft gradient from the game's hue, with its first character.
struct CoverView: View {
    let game: Game
    var letterSize: CGFloat = 48
    var showsLetter = true

    var body: some View {
        let h = game.coverHue
        ZStack {
            LinearGradient(
                colors: [
                    Color(hue: h, saturation: 0.45, brightness: 0.88),
                    Color(hue: (h + 0.11).truncatingRemainder(dividingBy: 1), saturation: 0.55, brightness: 0.58),
                ],
                startPoint: .topLeading, endPoint: .bottomTrailing)
            if showsLetter {
                Text(String(game.name.trimmingCharacters(in: .whitespaces).prefix(1)).uppercased())
                    .font(.system(size: letterSize, weight: .bold, design: .rounded))
                    .foregroundStyle(.white.opacity(0.92))
            }
        }
    }
}

extension GameStatus {
    var label: String {
        switch self {
        case .ready: return L("status_ready")
        case .keysMissing: return L("status_keys_missing")
        case .incomplete: return L("status_incomplete")
        }
    }

    var color: Color {
        switch self {
        case .ready: return .green
        case .keysMissing: return .orange
        case .incomplete: return .red
        }
    }
}

struct StatusBadge: View {
    let status: GameStatus

    var body: some View {
        let text = status.label, color = status.color
        Text(text)
            .font(.caption2.weight(.semibold))
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(.ultraThinMaterial, in: Capsule())
            .overlay(Capsule().strokeBorder(color.opacity(0.6)))
            .foregroundStyle(color)
    }
}

extension Color {
    static var cardBackground: Color {
        #if os(macOS)
        Color(nsColor: .controlBackgroundColor)
        #else
        Color(uiColor: .secondarySystemBackground)
        #endif
    }
}

extension View {
    /// The platform's standard grouped-list look for the detail and form screens.
    @ViewBuilder func groupedFormStyle() -> some View {
        #if os(macOS)
        self.formStyle(.grouped)
        #else
        self
        #endif
    }
}
