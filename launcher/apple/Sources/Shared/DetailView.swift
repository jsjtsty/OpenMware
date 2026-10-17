import SwiftUI
import UniformTypeIdentifiers

#if os(macOS)
import AppKit
#else
import UIKit
#endif

struct DetailView: View {
    @EnvironmentObject private var store: LibraryStore
    let gameID: String
    @Binding var path: [String]

    @State private var refresh = 0
    @State private var showRename = false
    @State private var newName = ""
    @State private var confirmClear = false
    @State private var confirmRemove = false
    @State private var pickKeys = false
    @State private var verifying = false
    @State private var verifyMessage: String?

    private var game: Game? { store.games.first { $0.id == gameID } }

    var body: some View {
        if let game {
            content(game)
        } else {
            Color.clear.onAppear { path.removeAll() } // removed
        }
    }

    private func content(_ game: Game) -> some View {
        layout(game)
        #if os(iOS)
        .navigationTitle(game.name)
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button { newName = game.name; showRename = true } label: { Label(L("detail_rename"), systemImage: "pencil") }
            }
        }
        .alert(L("detail_rename"), isPresented: $showRename) {
            TextField(L("add_name_label"), text: $newName)
            Button(L("save")) { store.rename(game, to: newName) }
            Button(L("cancel"), role: .cancel) {}
        }
        .confirmationDialog(L("confirm_clear_saves", game.name), isPresented: $confirmClear, titleVisibility: .visible) {
            Button(L("detail_clear_saves"), role: .destructive) { store.clearSaves(game); refresh += 1 }
        }
        .confirmationDialog(L("confirm_remove", game.name), isPresented: $confirmRemove, titleVisibility: .visible) {
            Button(L("detail_remove"), role: .destructive) { store.remove(game) }
        }
        .fileImporter(isPresented: $pickKeys, allowedContentTypes: [.plainText, .data]) { result in
            guard case .success(let url) = result else { return }
            let count = store.setKeys(game, text: store.readText(url))
            verifyMessage = count > 0 ? L("add_keys_ok", count) : L("add_keys_invalid")
            refresh += 1
        }
    }

    #if os(macOS)
    /// macOS: a banner with the cover and the play button, the facts as tiles, then the actions on cards.
    @ViewBuilder
    private func layout(_ game: Game) -> some View {
        let status = store.status(game)
        let (archiveCount, gameBytes) = store.gameBytes(game)
        let saveBytes = store.saveBytes(game)
        let keyCount = store.keyCount(game)
        let isPlaying = store.playing == game.id
        let _ = refresh

        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                GameHero(game: game, status: status) {
                    if isPlaying {
                        Button { store.stop() } label: { Label(L("stop"), systemImage: "stop.fill") }
                            .buttonStyle(HeroButtonStyle(prominent: false))
                    } else {
                        Button { store.play(game) } label: { Label(L("play"), systemImage: "play.fill") }
                            .buttonStyle(HeroButtonStyle())
                            .keyboardShortcut(.defaultAction)
                            .disabled(status != .ready || store.playing != nil)
                    }
                    Button { NSWorkspace.shared.activateFileViewerSelecting([store.folder(game)]) } label: { Label(L("detail_reveal"), systemImage: "folder") }
                        .buttonStyle(HeroButtonStyle(prominent: false))
                }

                switch status {
                case .keysMissing:
                    notice(L("cannot_play_keys"), icon: "key.slash", color: .orange)
                case .incomplete:
                    notice(L("cannot_play_incomplete"), icon: "exclamationmark.triangle.fill", color: .red)
                case .ready:
                    EmptyView()
                }

                VStack(alignment: .leading, spacing: 8) {
                    Text(L("detail_section_info")).font(.headline).padding(.leading, 4)
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 190), spacing: 12)], spacing: 12) {
                        StatTile(icon: "clock", title: L("detail_play_time"), value: game.playSeconds > 0 ? formatDuration(game.playSeconds) : L("never_played"))
                        StatTile(icon: "internaldrive", title: L("detail_size"), value: formatBytes(gameBytes))
                        StatTile(icon: "shippingbox", title: L("detail_archives"), value: "\(archiveCount)")
                        StatTile(icon: game.storage == .copied ? "square.on.square" : "link",
                                 title: L("detail_storage"), value: L(game.storage == .copied ? "storage_copied" : "storage_linked"))
                        StatTile(icon: "tray.full", title: L("detail_save_size"), value: saveBytes > 0 ? formatBytes(saveBytes) : L("detail_no_saves"))
                        StatTile(icon: "key", title: L("detail_keys"), value: keyCount > 0 ? L("add_keys_ok", keyCount) : L("add_keys_invalid"))
                    }
                }

                CardSection(title: L("detail_section_manage")) {
                    ActionRow(icon: "checkmark.shield", title: L("detail_verify"), busy: verifying) { verify(game) }
                        .disabled(verifying || status == .incomplete)
                    if let verifyMessage {
                        Text(verifyMessage)
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.horizontal, 44)
                            .padding(.bottom, 10)
                    }
                    Divider().padding(.leading, 44)
                    ActionRow(icon: "key", title: L("detail_keys_replace")) { pickKeys = true }
                    Divider().padding(.leading, 44)
                    ActionRow(icon: "square.and.arrow.up", title: L("detail_export_saves")) { exportSaves(game) }
                        .disabled(saveBytes == 0)
                }

                CardSection(title: L("detail_section_danger")) {
                    ActionRow(icon: "trash", title: L("detail_clear_saves"), destructive: true) { confirmClear = true }
                        .disabled(saveBytes == 0)
                    Divider().padding(.leading, 44)
                    ActionRow(icon: "xmark.bin", title: L("detail_remove"), destructive: true) { confirmRemove = true }
                }
            }
            .padding(24)
            .frame(maxWidth: 860)
            .frame(maxWidth: .infinity)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func notice(_ text: String, icon: String, color: Color) -> some View {
        Label(text, systemImage: icon)
            .font(.callout.weight(.medium))
            .foregroundStyle(color)
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(color.opacity(0.1), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
    #else
    @ViewBuilder
    private func layout(_ game: Game) -> some View {
        let status = store.status(game)
        let (archiveCount, gameBytes) = store.gameBytes(game)
        let saveBytes = store.saveBytes(game)
        let keyCount = store.keyCount(game)
        let isPlaying = store.playing == game.id
        let _ = refresh

        List {
            Section {
                VStack(spacing: 14) {
                    CoverView(game: game, letterSize: 72)
                        .frame(height: 170)
                        .clipShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
                    if isPlaying {
                        Label(L("playing"), systemImage: "play.fill").frame(maxWidth: .infinity)
                    } else {
                        Button { store.play(game) } label: { Label(L("play"), systemImage: "play.fill").frame(maxWidth: .infinity) }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.large)
                            .disabled(status != .ready || store.playing != nil)
                    }
                    switch status {
                    case .keysMissing: Label(L("cannot_play_keys"), systemImage: "key.slash").foregroundStyle(.orange)
                    case .incomplete: Label(L("cannot_play_incomplete"), systemImage: "exclamationmark.triangle").foregroundStyle(.red)
                    case .ready: EmptyView()
                    }
                }
                .listRowInsets(EdgeInsets(top: 12, leading: 12, bottom: 12, trailing: 12))
                .listRowBackground(Color.clear)
            }

            Section {
                row(L("detail_size"), formatBytes(gameBytes))
                row(L("detail_archives"), "\(archiveCount)")
                row(L("detail_storage"), L(game.storage == .copied ? "storage_copied" : "storage_linked"))
                row(L("detail_play_time"), game.playSeconds > 0 ? formatDuration(game.playSeconds) : L("never_played"))
                row(L("detail_save_size"), saveBytes > 0 ? formatBytes(saveBytes) : L("detail_no_saves"))
                row(L("detail_keys"), keyCount > 0 ? L("add_keys_ok", keyCount) : L("add_keys_invalid"))
            }

            Section {
                Button { verify(game) } label: {
                    HStack {
                        Label(L("detail_verify"), systemImage: "checkmark.shield")
                        if verifying { Spacer(); ProgressView().controlSize(.small) }
                    }
                }
                .disabled(verifying || status == .incomplete)
                if let verifyMessage {
                    Text(verifyMessage).font(.callout).foregroundStyle(.secondary)
                }
                Button { pickKeys = true } label: { Label(L("detail_keys_replace"), systemImage: "key") }
                Button { exportSaves(game) } label: { Label(L("detail_export_saves"), systemImage: "square.and.arrow.up") }
                    .disabled(saveBytes == 0)
            }

            Section {
                Button(role: .destructive) { confirmClear = true } label: { Label(L("detail_clear_saves"), systemImage: "trash") }
                    .disabled(saveBytes == 0)
                Button(role: .destructive) { confirmRemove = true } label: { Label(L("detail_remove"), systemImage: "trash.fill") }
            }
        }
    }
    #endif

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
            Spacer()
            Text(value).foregroundStyle(.secondary).multilineTextAlignment(.trailing)
        }
    }

    /// Opens the archives with the keys in a background task and reports the result.
    private func verify(_ game: Game) {
        verifying = true
        verifyMessage = nil
        guard let access = store.openGameFolder(game) else {
            verifying = false
            verifyMessage = L("error_folder_unavailable")
            return
        }
        let keys = store.keysURL(game)
        Task.detached {
            let result = Engine.probe(gameDir: access.url, keys: keys)
            access.stop()
            await MainActor.run {
                verifying = false
                verifyMessage = result.ok ? L("verify_ok", result.archives) : L("verify_failed", result.message)
            }
        }
    }

    private func exportSaves(_ game: Game) {
        guard let zip = store.exportSaves(game) else { return }
        #if os(macOS)
        let panel = NSSavePanel()
        panel.nameFieldStringValue = zip.lastPathComponent
        panel.allowedContentTypes = [.zip]
        if panel.runModal() == .OK, let dest = panel.url {
            try? FileManager.default.removeItem(at: dest)
            try? FileManager.default.copyItem(at: zip, to: dest)
        }
        #else
        let controller = UIActivityViewController(activityItems: [zip], applicationActivities: nil)
        let scene = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first
        if let top = scene?.windows.first(where: \.isKeyWindow)?.rootViewController {
            var presenter = top
            while let next = presenter.presentedViewController { presenter = next }
            controller.popoverPresentationController?.sourceView = presenter.view
            presenter.present(controller, animated: true)
        }
        #endif
    }
}
