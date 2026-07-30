# AM Download

A small native macOS front end for `am-download.sh`. It downloads audio from
SoundCloud, Instagram, and X/Twitter as M4A, with optional metadata and an
explicit-content flag.

Available source metadata, artwork, and chapters are embedded automatically.
Metadata fields left blank preserve the source values; a value entered in the
app replaces only that specific field.

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
