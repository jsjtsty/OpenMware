#if os(macOS)
import SwiftUI

/// The macOS window: the games in a sidebar, the selected one on the right (iOS keeps the grid in LibraryView).
struct MacLibraryView: View {
    @EnvironmentObject private var store: LibraryStore
    @State private var selection: String?
    @State private var showAdd = false
    @State private var showAbout = false

    var body: some View {
        NavigationSplitView {
            List(selection: $selection) {
                ForEach(store.games) { game in
                    SidebarRow(game: game).tag(game.id)
                }
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 210, ideal: 240, max: 320)
            .safeAreaInset(edge: .bottom) {
                Button { showAdd = true } label: {
                    Label(L("add_game"), systemImage: "plus.circle.fill")
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
            }
        } detail: {
            if let id = selection, store.games.contains(where: { $0.id == id }) {
                DetailView(gameID: id, path: Binding(get: { [id] }, set: { selection = $0.last }))
                    .id(id)
            } else if store.games.isEmpty {
                EmptyLibrary { showAdd = true }
            } else {
                Text(L("select_game"))
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .navigationTitle(L("library_title"))
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button { showAdd = true } label: { Label(L("add_game"), systemImage: "plus") }
            }
            ToolbarItem(placement: .automatic) {
                Button { showAbout = true } label: { Label(L("menu_about"), systemImage: "info.circle") }
            }
        }
        .sheet(isPresented: $showAdd) {
            AddGameView { id in selection = id }
                .frame(minWidth: 480, minHeight: 560)
        }
        .sheet(isPresented: $showAbout) { AboutView() }
        .alert(L("error_title"), isPresented: Binding(get: { store.lastError != nil }, set: { if !$0 { store.lastError = nil } })) {
            Button(L("ok"), role: .cancel) {}
        } message: {
            Text(store.lastError ?? "")
        }
        .onAppear { if selection == nil { selection = store.games.first?.id } }
    }
}

private struct SidebarRow: View {
    @EnvironmentObject private var store: LibraryStore
    let game: Game

    var body: some View {
        let playing = store.playing == game.id
        let status = store.status(game)
        HStack(spacing: 10) {
            CoverView(game: game, letterSize: 15)
                .frame(width: 30, height: 40)
                .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
            VStack(alignment: .leading, spacing: 2) {
                Text(game.name).lineLimit(1)
                HStack(spacing: 5) {
                    Circle().fill(playing ? Color.accentColor : status.color).frame(width: 6, height: 6)
                    Text(playing ? L("playing") : status.label)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(.vertical, 3)
    }
}

// MARK: Detail building blocks

/// The banner at the top of a game's page: its cover, name, status and the play button.
struct GameHero<Actions: View>: View {
    let game: Game
    let status: GameStatus
    @ViewBuilder var actions: Actions

    var body: some View {
        HStack(alignment: .bottom, spacing: 22) {
            CoverView(game: game, letterSize: 64)
                .frame(width: 132, height: 176)
                .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).strokeBorder(.white.opacity(0.25)))
                .shadow(color: .black.opacity(0.3), radius: 12, y: 6)
            VStack(alignment: .leading, spacing: 8) {
                StatusBadge(status: status)
                    .environment(\.colorScheme, .dark)
                Text(game.name)
                    .font(.system(size: 28, weight: .bold))
                    .foregroundStyle(.white)
                    .lineLimit(2)
                Text(game.lastPlayedAt.map { L("last_played", $0.formatted(.relative(presentation: .named))) } ?? L("never_played"))
                    .font(.callout)
                    .foregroundStyle(.white.opacity(0.8))
                HStack(spacing: 10) { actions }
                    .padding(.top, 6)
            }
            Spacer(minLength: 0)
        }
        .padding(24)
        .frame(maxWidth: .infinity, minHeight: 224, alignment: .bottomLeading)
        .background {
            ZStack {
                CoverView(game: game, showsLetter: false)
                LinearGradient(colors: [.black.opacity(0.05), .black.opacity(0.45)], startPoint: .top, endPoint: .bottom)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
    }
}

/// The capsule buttons on the banner: solid white for the main action, translucent for the others.
struct HeroButtonStyle: ButtonStyle {
    var prominent = true
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.headline)
            .padding(.horizontal, 22)
            .padding(.vertical, 9)
            .foregroundStyle(prominent ? Color.black.opacity(0.85) : Color.white)
            .background(Capsule().fill(prominent ? Color.white : Color.white.opacity(0.22)))
            .opacity(isEnabled ? (configuration.isPressed ? 0.75 : 1) : 0.45)
            .contentShape(Capsule())
    }
}

struct StatTile: View {
    let icon: String
    let title: String
    let value: String

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(Color.accentColor)
                .frame(width: 30, height: 30)
                .background(Color.accentColor.opacity(0.12), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.caption).foregroundStyle(.secondary)
                Text(value).font(.callout.weight(.medium)).lineLimit(1).truncationMode(.middle)
            }
            Spacer(minLength: 0)
        }
        .padding(12)
        .background(Color.cardBackground, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Color.primary.opacity(0.07)))
    }
}

/// A titled group of rows on a rounded card, like a grouped form.
struct CardSection<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.headline).padding(.leading, 4)
            VStack(spacing: 0) { content }
                .background(Color.cardBackground, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Color.primary.opacity(0.07)))
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
        }
    }
}

struct ActionRow: View {
    let icon: String
    let title: String
    var destructive = false
    var busy = false
    let action: () -> Void
    @State private var hovering = false
    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Image(systemName: icon)
                    .frame(width: 20)
                    .foregroundStyle(destructive ? Color.red : Color.accentColor)
                Text(title).foregroundStyle(destructive ? Color.red : Color.primary)
                Spacer()
                if busy {
                    ProgressView().controlSize(.small)
                } else {
                    Image(systemName: "chevron.right").font(.caption.weight(.semibold)).foregroundStyle(.tertiary)
                }
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .contentShape(Rectangle())
            .background(hovering && isEnabled ? Color.primary.opacity(0.05) : Color.clear)
        }
        .buttonStyle(.plain)
        .opacity(isEnabled ? 1 : 0.4)
        .onHover { hovering = $0 }
    }
}
#endif
