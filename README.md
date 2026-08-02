# AM Download

Native macOS and Windows front ends for `am-download.sh`. AM Download creates
tagged M4A files from public music and social sources for addition to an Apple
Music library.

Tested sources:

- SoundCloud
- Bandcamp
- Audiomack
- Mixcloud
- HearThisAt
- Vocaroo and `voca.ro`
- Instagram
- X/Twitter

Available source metadata, artwork, and chapters are embedded automatically.
Metadata fields left blank preserve the source values; a value entered in the
app replaces only that specific field. Overrides are available for title,
artist, album, album artist, genre, composer, release date, track number, disc
number, and the explicit-content advisory.

On launch, the app automatically fills the source field when the clipboard
contains a valid link from one of the tested sources.

## Requirements

- macOS 13 or newer on Apple silicon
- `yt-dlp`, `ffmpeg`, and `AtomicParsley`

Install the command-line dependencies with:

```sh
brew install yt-dlp ffmpeg atomicparsley
```

## Build

```sh
chmod +x build-app.sh
./build-app.sh
```

The native application is created at `build/AM Download.app`. Move it to
`/Applications` if desired. The app is ad-hoc signed for local use.

The original `am-download.sh` remains available for terminal use.

## Windows

The Windows port is a native Win32 desktop executable. It keeps the same
source detection, clipboard paste, metadata overrides, folder selection,
progress log, cancellation, and M4A post-processing workflow as the macOS
app.

Requirements:

- Windows 10 or newer
- Visual Studio with the Desktop development with C++ workload (to build)
- `yt-dlp.exe`, `ffmpeg.exe`, and `AtomicParsley.exe`

The Windows app searches for the command-line tools in its own folder, a
`tools` subfolder, and the system `PATH`. Build it from PowerShell with:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build-windows.ps1
```

The executable is created at `build\windows\AMDownload.exe`. The app does not
bundle the third-party command-line tools, so copy them into `build\windows\tools`
or add their directory to `PATH` before downloading.

To build the Windows installer, run the same command. It creates
`build\installer\AMDownloadSetup.exe`. Running the installer requires
administrator approval and installs to `Program Files\AM Download`, registers an
uninstaller, and creates Start Menu, public desktop, and Quick Launch shortcuts.

## GitHub releases

The GitHub Actions workflow builds the macOS app and Windows portable binary and
installer, then attaches them to a GitHub release. Push a semantic-version tag
to publish a release:

```sh
git tag v1.0.0
git push origin v1.0.0
```

The workflow can also be started manually from the Actions tab by providing a
tag such as `v1.0.0`.
