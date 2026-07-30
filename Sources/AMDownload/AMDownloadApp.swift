import AppKit
import SwiftUI

@main
struct AMDownloadApp: App {
    @StateObject private var model = DownloadModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(model)
                .frame(minWidth: 600, idealWidth: 680, minHeight: 620, idealHeight: 700)
        }
        .windowResizability(.contentSize)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}

struct ContentView: View {
    @EnvironmentObject private var model: DownloadModel
    @FocusState private var urlIsFocused: Bool

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 22) {
                    sourceSection
                    metadataSection
                    destinationSection
                    activitySection
                }
                .padding(24)
            }
            Divider()
            footer
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .onAppear {
            model.autoPasteSupportedURL()
            urlIsFocused = true
            model.refreshDependencies()
        }
        .alert("Download Error", isPresented: $model.showingError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(model.errorMessage)
        }
    }

    private var header: some View {
        HStack(spacing: 14) {
            Image(systemName: "arrow.down.circle.fill")
                .font(.system(size: 36))
                .symbolRenderingMode(.hierarchical)
                .foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 2) {
                Text("AM Download")
                    .font(.title2.weight(.semibold))
                Text("Create tagged M4A files for Apple Music")
                    .foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(20)
    }

    private var sourceSection: some View {
        FormSection(title: "Source", systemImage: "link") {
            HStack {
                TextField("Paste a supported music or social link", text: $model.urlString)
                    .textFieldStyle(.roundedBorder)
                    .focused($urlIsFocused)
                    .onSubmit { model.startDownload() }
                Button("Paste") {
                    model.pasteURL()
                }
            }
            if let platform = model.detectedPlatform {
                Label(platform, systemImage: "checkmark.circle.fill")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Text("SoundCloud · Bandcamp · Audiomack · Mixcloud · HearThisAt · Vocaroo · Instagram · X")
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
    }

    private var metadataSection: some View {
        FormSection(title: "Metadata", systemImage: "music.note.list") {
            Text("Source metadata is kept automatically. Enter only the values you want to replace.")
                .font(.caption)
                .foregroundStyle(.secondary)
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 10) {
                metadataRow("Title", text: $model.title)
                metadataRow("Artist", text: $model.artist)
                metadataRow("Album", text: $model.album)
                metadataRow("Album Artist", text: $model.albumArtist)
                metadataRow("Genre", text: $model.genre)
            }
            DisclosureGroup("More Apple Music tags", isExpanded: $model.showMoreTags) {
                Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 10) {
                    metadataRow("Composer", text: $model.composer)
                    metadataRow("Release Date", text: $model.releaseDate, prompt: "Keep source value (e.g. 2026)")
                    metadataRow("Track", text: $model.trackNumber, prompt: "Keep source value (e.g. 3/12)")
                    metadataRow("Disc", text: $model.discNumber, prompt: "Keep source value (e.g. 1/2)")
                }
                .padding(.top, 10)
            }
            Toggle("Mark as explicit (off keeps the source value)", isOn: $model.isExplicit)
        }
    }

    private func metadataRow(
        _ label: String,
        text: Binding<String>,
        prompt: String = "Keep source value"
    ) -> some View {
        GridRow {
            Text(label)
                .foregroundStyle(.secondary)
                .frame(width: 82, alignment: .trailing)
            TextField(prompt, text: text)
                .textFieldStyle(.roundedBorder)
        }
    }

    private var destinationSection: some View {
        FormSection(title: "Save To", systemImage: "folder") {
            HStack {
                Image(systemName: "folder.fill")
                    .foregroundStyle(.secondary)
                Text(model.outputDirectory.path)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer()
                Button("Choose…") {
                    model.chooseOutputDirectory()
                }
            }
        }
    }

    private var activitySection: some View {
        FormSection(title: "Activity", systemImage: "terminal") {
            ScrollViewReader { proxy in
                ScrollView {
                    Text(model.log.isEmpty ? "Ready to download." : model.log)
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(model.log.isEmpty ? .secondary : .primary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .topLeading)
                        .id("logEnd")
                }
                .frame(height: 145)
                .padding(10)
                .background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
                .overlay {
                    RoundedRectangle(cornerRadius: 8)
                        .stroke(Color(nsColor: .separatorColor))
                }
                .onChange(of: model.log) { _ in
                    proxy.scrollTo("logEnd", anchor: .bottom)
                }
            }
        }
    }

    private var footer: some View {
        HStack {
            Label(model.dependencySummary, systemImage: model.dependenciesReady ? "checkmark.circle" : "exclamationmark.triangle")
                .font(.caption)
                .foregroundStyle(model.dependenciesReady ? Color.secondary : Color.orange)
            Spacer()
            if model.isRunning {
                ProgressView()
                    .controlSize(.small)
                Button("Cancel", role: .destructive) {
                    model.cancelDownload()
                }
            } else {
                Button("Show Downloads") {
                    NSWorkspace.shared.activateFileViewerSelecting([model.outputDirectory])
                }
                Button("Download") {
                    model.startDownload()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(model.urlString.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
        }
        .padding(16)
    }
}

struct FormSection<Content: View>: View {
    let title: String
    let systemImage: String
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label(title, systemImage: systemImage)
                .font(.headline)
            content
        }
    }
}

@MainActor
final class DownloadModel: ObservableObject {
    @Published var urlString = ""
    @Published var title = ""
    @Published var artist = ""
    @Published var album = ""
    @Published var albumArtist = ""
    @Published var genre = ""
    @Published var composer = ""
    @Published var releaseDate = ""
    @Published var trackNumber = ""
    @Published var discNumber = ""
    @Published var showMoreTags = false
    @Published var isExplicit = false
    @Published var outputDirectory: URL
    @Published var log = ""
    @Published var isRunning = false
    @Published var showingError = false
    @Published var errorMessage = ""
    @Published private(set) var missingDependencies: [String] = []

    private var process: Process?
    private var outputPipe: Pipe?

    init() {
        outputDirectory = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser
    }

    var detectedPlatform: String? {
        guard let platform = platformName(for: urlString) else { return nil }
        return "\(platform) link detected"
    }

    var dependenciesReady: Bool { missingDependencies.isEmpty }

    var dependencySummary: String {
        dependenciesReady ? "Required tools are installed" : "Missing: \(missingDependencies.joined(separator: ", "))"
    }

    func refreshDependencies() {
        missingDependencies = ["yt-dlp", "ffmpeg", "AtomicParsley"].filter { executable(named: $0) == nil }
    }

    func pasteURL() {
        if let pasted = NSPasteboard.general.string(forType: .string) {
            urlString = pasted.trimmingCharacters(in: .whitespacesAndNewlines)
        }
    }

    func autoPasteSupportedURL() {
        guard urlString.isEmpty,
              let pasted = NSPasteboard.general.string(forType: .string) else {
            return
        }
        let candidate = pasted.trimmingCharacters(in: .whitespacesAndNewlines)
        if platformName(for: candidate) != nil {
            urlString = candidate
        }
    }

    func chooseOutputDirectory() {
        let panel = NSOpenPanel()
        panel.title = "Choose Download Folder"
        panel.prompt = "Choose"
        panel.directoryURL = outputDirectory
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let selected = panel.url {
            outputDirectory = selected
        }
    }

    func startDownload() {
        guard !isRunning else { return }
        let sourceURL = urlString.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let parsedURL = URL(string: sourceURL),
              let scheme = parsedURL.scheme?.lowercased(),
              scheme == "https" || scheme == "http" else {
            presentError("Enter a valid web URL.")
            return
        }
        guard isValidIndex(trackNumber) else {
            presentError("Track must be a number or number/total, such as 3 or 3/12.")
            return
        }
        guard isValidIndex(discNumber) else {
            presentError("Disc must be a number or number/total, such as 1 or 1/2.")
            return
        }

        refreshDependencies()
        guard dependenciesReady,
              let ytDLP = executable(named: "yt-dlp"),
              let ffmpeg = executable(named: "ffmpeg"),
              let atomicParsley = executable(named: "AtomicParsley") else {
            presentError("Install the missing tools with Homebrew, then reopen the app:\n\nbrew install \(missingDependencies.joined(separator: " "))")
            return
        }

        var arguments = [
            "--newline",
            "--progress",
            "--format", "bestaudio[ext=m4a]/bestaudio[ext=aac]/bestaudio/best",
            "--extract-audio",
            "--audio-format", "m4a",
            "--audio-quality", "0",
            "--embed-thumbnail",
            "--embed-metadata",
            "--embed-chapters",
            "--add-metadata",
            "--ffmpeg-location", ffmpeg.deletingLastPathComponent().path
        ]

        let lowercasedURL = sourceURL.lowercased()
        let outputTemplate: String
        if lowercasedURL.contains("instagram.com") || lowercasedURL.contains("instagr.am")
            || lowercasedURL.contains("x.com") || lowercasedURL.contains("twitter.com") {
            outputTemplate = "%(title).100B.%(ext)s"
        } else if lowercasedURL.contains("/sets/") {
            outputTemplate = "%(playlist_title)s/%(title)s.%(ext)s"
        } else {
            outputTemplate = "%(title)s.%(ext)s"
        }

        var tagArguments: [(String, String)] = []
        if !title.isEmpty { tagArguments.append(("--title", title)) }
        if !artist.isEmpty { tagArguments.append(("--artist", artist)) }
        if !album.isEmpty { tagArguments.append(("--album", album)) }
        if !albumArtist.isEmpty { tagArguments.append(("--albumArtist", albumArtist)) }
        if !genre.isEmpty { tagArguments.append(("--genre", genre)) }
        if !composer.isEmpty { tagArguments.append(("--composer", composer)) }
        if !releaseDate.isEmpty { tagArguments.append(("--year", releaseDate)) }
        if !trackNumber.isEmpty { tagArguments.append(("--tracknum", trackNumber)) }
        if !discNumber.isEmpty { tagArguments.append(("--disk", discNumber)) }
        if isExplicit { tagArguments.append(("--advisory", "explicit")) }

        if !tagArguments.isEmpty {
            let tags = tagArguments
                .map { "\($0.0) \(shellQuote($0.1))" }
                .joined(separator: " ")
            let command = "\(shellQuote(atomicParsley.path)) {} \(tags) --overWrite"
            arguments += ["--exec", "after_move:\(command)"]
        }

        arguments += ["--output", outputTemplate, sourceURL]

        let task = Process()
        let pipe = Pipe()
        task.executableURL = ytDLP
        task.arguments = arguments
        task.currentDirectoryURL = outputDirectory
        task.standardOutput = pipe
        task.standardError = pipe
        var environment = ProcessInfo.processInfo.environment
        environment["PATH"] = searchPaths.joined(separator: ":")
        task.environment = environment

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty, let text = String(data: data, encoding: .utf8) else { return }
            Task { @MainActor [weak self] in
                self?.log.append(text)
            }
        }
        task.terminationHandler = { [weak self] completedTask in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.outputPipe?.fileHandleForReading.readabilityHandler = nil
                self.isRunning = false
                self.process = nil
                self.outputPipe = nil
                if completedTask.terminationStatus == 0 {
                    self.log.append("\nDone.\n")
                    NSSound(named: "Glass")?.play()
                } else if completedTask.terminationReason == .uncaughtSignal {
                    self.log.append("\nCancelled.\n")
                } else {
                    self.presentError("The download did not complete. Check the activity log for details.")
                }
            }
        }

        log = "Downloading \(sourceURL)\n\n"
        process = task
        outputPipe = pipe
        isRunning = true
        do {
            try task.run()
        } catch {
            pipe.fileHandleForReading.readabilityHandler = nil
            process = nil
            outputPipe = nil
            isRunning = false
            presentError("Could not start yt-dlp: \(error.localizedDescription)")
        }
    }

    func cancelDownload() {
        process?.terminate()
    }

    private func presentError(_ message: String) {
        errorMessage = message
        showingError = true
    }

    private var searchPaths: [String] {
        var paths = [
            "/opt/homebrew/bin",
            "/usr/local/bin",
            "/usr/bin",
            "/bin",
            "/Library/Frameworks/Python.framework/Versions/Current/bin",
            "/Library/Frameworks/Python.framework/Versions/3.14/bin",
            "/Library/Frameworks/Python.framework/Versions/3.13/bin",
            "/Library/Frameworks/Python.framework/Versions/3.12/bin"
        ]
        if let existing = ProcessInfo.processInfo.environment["PATH"] {
            paths.append(contentsOf: existing.split(separator: ":").map(String.init))
        }
        return Array(NSOrderedSet(array: paths)) as? [String] ?? paths
    }

    private func executable(named name: String) -> URL? {
        for directory in searchPaths {
            let candidate = URL(fileURLWithPath: directory).appendingPathComponent(name)
            if FileManager.default.isExecutableFile(atPath: candidate.path) {
                return candidate
            }
        }
        return nil
    }

    private func shellQuote(_ value: String) -> String {
        "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    private func platformName(for value: String) -> String? {
        guard let url = URL(string: value),
              let scheme = url.scheme?.lowercased(),
              scheme == "https" || scheme == "http",
              let host = url.host?.lowercased() else {
            return nil
        }
        if host == "soundcloud.com" || host.hasSuffix(".soundcloud.com") {
            return "SoundCloud"
        }
        if host == "bandcamp.com" || host.hasSuffix(".bandcamp.com") {
            return "Bandcamp"
        }
        if host == "audiomack.com" || host.hasSuffix(".audiomack.com") {
            return "Audiomack"
        }
        if host == "mixcloud.com" || host.hasSuffix(".mixcloud.com") {
            return "Mixcloud"
        }
        if host == "hearthis.at" || host.hasSuffix(".hearthis.at") {
            return "HearThisAt"
        }
        if host == "vocaroo.com" || host.hasSuffix(".vocaroo.com")
            || host == "voca.ro" || host.hasSuffix(".voca.ro") {
            return "Vocaroo"
        }
        if host == "instagram.com" || host.hasSuffix(".instagram.com")
            || host == "instagr.am" || host.hasSuffix(".instagr.am") {
            return "Instagram"
        }
        if host == "x.com" || host.hasSuffix(".x.com")
            || host == "twitter.com" || host.hasSuffix(".twitter.com") {
            return "X"
        }
        return nil
    }

    private func isValidIndex(_ value: String) -> Bool {
        if value.isEmpty { return true }
        return value.range(of: #"^\d+(\/\d+)?$"#, options: .regularExpression) != nil
    }
}
