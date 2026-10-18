#if os(iOS)
import UIKit
import UniformTypeIdentifiers

/// The system document picker, presented directly. SwiftUI's `fileImporter` sometimes never reports the choice for a folder
/// (the picker stays open after "Open"), so iOS uses this instead.
final class DocumentPicker: NSObject, UIDocumentPickerDelegate {
    private static var active: DocumentPicker? // the picker holds its delegate weakly
    private let onPick: (URL) -> Void

    private init(onPick: @escaping (URL) -> Void) { self.onPick = onPick }

    static func present(types: [UTType], onPick: @escaping (URL) -> Void) {
        let scene = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first
        guard let top = scene?.windows.first(where: \.isKeyWindow)?.rootViewController else { return }
        var presenter = top
        while let next = presenter.presentedViewController { presenter = next }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: false)
        let delegate = DocumentPicker(onPick: onPick)
        picker.delegate = delegate
        picker.allowsMultipleSelection = false
        active = delegate
        presenter.present(picker, animated: true)
    }

    func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        DocumentPicker.active = nil
        if let url = urls.first { onPick(url) }
    }

    func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { DocumentPicker.active = nil }
}
#endif
