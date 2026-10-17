import SwiftUI
import UniformTypeIdentifiers

struct LibraryView: View {
    @EnvironmentObject private var store: LibraryStore
    @State private var path: [String] = []
    @State private var showAdd = false
    @State private var showAbout = false

    private let columns = [GridItem(.adaptive(minimum: 160, maximum: 220), spacing: 16)]

    var body: some View {
        NavigationStack(path: $path) {
            Group {
                if store.games.isEmpty {
                    EmptyLibrary { showAdd = true }
                } else {
                    ScrollView {
                        LazyVGrid(columns: columns, spacing: 16) {
                            ForEach(store.games) { game in
                                NavigationLink(value: game.id) { GameCard(game: game) }
                                    .buttonStyle(.plain)
                            }
                        }
                        .padding(16)
                    }
                }
            }
            .navigationTitle(L("library_title"))
            .navigationDestination(for: String.self) { id in DetailView(gameID: id, path: $path) }
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button { showAdd = true } label: { Label(L("add_game"), systemImage: "plus") }
                }
                ToolbarItem(placement: .automatic) {
                    Button { showAbout = true } label: { Label(L("menu_about"), systemImage: "info.circle") }
                }
            }
            .sheet(isPresented: $showAdd) {
                AddGameView { id in path = [id] }
                    #if os(macOS)
                    .frame(minWidth: 480, minHeight: 560)
                    #endif
            }
            .sheet(isPresented: $showAbout) { AboutView() }
        }
        .alert(L("error_title"), isPresented: Binding(get: { store.lastError != nil }, set: { if !$0 { store.lastError = nil } })) {
            Button(L("ok"), role: .cancel) {}
        } message: {
            Text(store.lastError ?? "")
        }
    }
}

private struct EmptyLibrary: View {
    let onAdd: () -> Void

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "gamecontroller")
                .font(.system(size: 64))
                .foregroundStyle(.tint)
            Text(L("empty_title")).font(.title2.weight(.semibold))
            Text(L("empty_body"))
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 360)
            Button(action: onAdd) { Label(L("add_game"), systemImage: "plus") }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .padding(.top, 8)
        }
        .padding(32)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

private struct GameCard: View {
    @EnvironmentObject private var store: LibraryStore
    let game: Game
    @State private var status: GameStatus = .ready

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            CoverView(game: game)
                .aspectRatio(3.0 / 4.0, contentMode: .fit)
                .overlay(alignment: .topTrailing) { StatusBadge(status: status).padding(8) }
                .overlay(alignment: .bottomLeading) {
                    if store.playing == game.id {
                        Label(L("playing"), systemImage: "play.fill")
                            .font(.caption.weight(.semibold))
                            .padding(6)
                            .background(.regularMaterial, in: Capsule())
                            .padding(8)
                    }
                }
            VStack(alignment: .leading, spacing: 2) {
                Text(game.name).font(.headline).lineLimit(2)
                Text(game.lastPlayedAt.map { L("last_played", $0.formatted(.relative(presentation: .named))) } ?? L("never_played"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(12)
        }
        .background(Color.cardBackground, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .clipShape(RoundedCornerShape(radius: 18))
        .shadow(color: .black.opacity(0.08), radius: 6, y: 2)
        .task(id: game) { status = store.status(game) }
    }
}

private struct RoundedCornerShape: Shape {
    let radius: CGFloat
    func path(in rect: CGRect) -> Path { RoundedRectangle(cornerRadius: radius, style: .continuous).path(in: rect) }
}
