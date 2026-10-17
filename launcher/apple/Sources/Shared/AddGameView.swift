import SwiftUI
import UniformTypeIdentifiers

struct AddGameView: View {
    @EnvironmentObject private var store: LibraryStore
    @Environment(\.dismiss) private var dismiss
    var onAdded: (String) -> Void

    private enum Importer { case folder, keys }

    @State private var importer: Importer?
    @State private var scan: FolderScan?
    @State private var name = ""
    @State private var keysText = ""
    @State private var copy = false
    @State private var copied: Int64 = 0
    @State private var working: Task<Void, Never>?
    @State private var error: String?

    private var keyCount: Int { Keys.parse(keysText).count }
    private var enoughSpace: Bool { !copy || scan == nil || store.freeBytes() > (scan?.totalBytes ?? 0) + (64 << 20) }
    private var canAdd: Bool { (scan?.isGame ?? false) && enoughSpace && working == nil }

    var body: some View {
        NavigationStack {
            Form {
                Section(L("add_step_folder")) {
                    Text(L("add_folder_hint")).font(.callout).foregroundStyle(.secondary)
                    Button { importer = .folder } label: {
                        Label(L(scan == nil ? "add_choose_folder" : "add_change_folder"), systemImage: "folder")
                    }
                    if let scan {
                        if scan.isGame {
                            Label(L("add_found", scan.archives.count, formatBytes(scan.totalBytes)), systemImage: "checkmark.circle.fill")
                                .foregroundStyle(.green)
                        } else {
                            Label(L("add_not_a_game"), systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red)
                        }
                    }
                    Toggle(L("add_copy"), isOn: $copy)
                    Text(L(copy ? "add_copy_on" : "add_copy_off")).font(.caption).foregroundStyle(.secondary)
                    if !enoughSpace, let scan {
                        Label(L("add_no_space", formatBytes(scan.totalBytes)), systemImage: "externaldrive.badge.exclamationmark").foregroundStyle(.red)
                    }
                }

                Section(L("add_step_name")) {
                    TextField(L("add_name_label"), text: $name)
                }

                Section(L("add_step_keys")) {
                    Button { importer = .keys } label: { Label(L("add_keys_import"), systemImage: "key") }
                    Text(L("add_keys_paste")).font(.caption).foregroundStyle(.secondary)
                    TextEditor(text: $keysText)
                        .font(.system(.caption, design: .monospaced))
                        .frame(minHeight: 90)
                        .autocorrectionDisabledCompat()
                    if !keysText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                        if keyCount > 0 {
                            Label(L("add_keys_ok", keyCount), systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                        } else {
                            Label(L("add_keys_invalid"), systemImage: "xmark.octagon.fill").foregroundStyle(.red)
                        }
                    }
                }

                if let error {
                    Section { Label(L("add_failed", error), systemImage: "exclamationmark.triangle.fill").foregroundStyle(.red) }
                }
                if working != nil {
                    Section {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(copy ? L("add_copying") : L("add_linking"))
                            if copy, let total = scan?.totalBytes, total > 0 {
                                ProgressView(value: Double(copied), total: Double(total))
                                Text(L("add_copy_progress", formatBytes(copied), formatBytes(total))).font(.caption).foregroundStyle(.secondary)
                            } else {
                                ProgressView()
                            }
                        }
                    }
                }
            }
            .groupedFormStyle()
            .navigationTitle(L("add_title"))
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(L("cancel")) {
                        working?.cancel()
                        dismiss()
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(L("add_import")) { startAdding() }.disabled(!canAdd)
                }
            }
            .fileImporter(
                isPresented: Binding(get: { importer != nil }, set: { if !$0 { importer = nil } }),
                allowedContentTypes: importer == .folder ? [UTType.folder] : [UTType.plainText, UTType.data],
                allowsMultipleSelection: false
            ) { result in
                guard case .success(let urls) = result, let url = urls.first else { return }
                switch importer {
                case .folder:
                    let s = store.scan(url)
                    scan = s
                    if name.isEmpty { name = s.displayName }
                    error = nil
                case .keys:
                    keysText = store.readText(url)
                case nil:
                    break
                }
            }
            .interactiveDismissDisabled(working != nil)
        }
    }

    private func startAdding() {
        guard let scan else { return }
        error = nil
        copied = 0
        working = Task {
            do {
                let game = try await store.add(scan, name: name, keysText: keysText, copy: copy) { bytes in
                    Task { @MainActor in copied = bytes }
                }
                working = nil
                dismiss()
                onAdded(game.id)
            } catch is CancellationError {
                working = nil
            } catch {
                working = nil
                self.error = error.localizedDescription
            }
        }
    }
}

private extension View {
    @ViewBuilder func autocorrectionDisabledCompat() -> some View {
        if #available(iOS 16.0, macOS 13.0, *) { self.autocorrectionDisabled() } else { self }
    }
}
