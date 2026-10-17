import SwiftUI

struct AboutView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text("OpenMware").font(.largeTitle.weight(.bold))
                    Text(L("about_version", Engine.version)).foregroundStyle(.secondary)
                    Text(L("about_body"))
                    Text(L("about_licenses")).font(.headline).padding(.top, 8)
                    Text(L("about_licenses_body")).font(.callout)
                    Text(L("about_ffmpeg")).font(.callout).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(20)
            }
            .navigationTitle(L("about_title"))
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button(L("close")) { dismiss() } }
            }
        }
        #if os(macOS)
        .frame(width: 460, height: 420)
        #endif
    }
}
