# AM Download

A small native macOS front end for `am-download.sh`. It creates tagged M4A
files from public music and social sources for addition to an Apple Music
library.

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
