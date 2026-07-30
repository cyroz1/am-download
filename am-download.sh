#!/usr/bin/env bash
set -euo pipefail

# Music and social audio downloader
# Usage: ./am-download.sh <supported_url>

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

usage() {
    echo "Usage: $0 <supported_url>"
    echo "  SoundCloud: downloads a track or playlist and converts to M4A with cover art."
    echo "  Music: supports public Bandcamp, Audiomack, Mixcloud, HearThisAt, and Vocaroo links."
    echo "  Instagram: extracts audio from post/reel/video media and converts to M4A."
    echo "  X/Twitter: extracts audio from post video media and converts to M4A."
    exit 1
}

check_deps() {
    local platform="${1:-}"
    local missing=()

    if ! command -v yt-dlp &>/dev/null; then
        missing+=("yt-dlp")
    fi
    if ! command -v ffmpeg &>/dev/null; then
        missing+=("ffmpeg")
    fi
    if ! command -v AtomicParsley &>/dev/null; then
        missing+=("AtomicParsley")
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
        echo -e "${RED}Missing dependencies: ${missing[*]}${NC}"
        echo ""
        echo "Install with Homebrew (macOS):"
        for dep in "${missing[@]}"; do
            echo "  brew install $dep"
        done
        echo ""
        echo "Install with apt (Debian/Ubuntu):"
        for dep in "${missing[@]}"; do
            if [[ "$dep" == "yt-dlp" ]]; then
                echo "  pip install yt-dlp   # or: sudo curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp && sudo chmod +x /usr/local/bin/yt-dlp"
            else
                echo "  sudo apt install $dep"
            fi
        done
        exit 1
    fi
}

[[ $# -lt 1 ]] && usage

URL="$1"

PLATFORM="unknown"
if [[ "$URL" == *"soundcloud.com"* ]]; then
    PLATFORM="soundcloud"
elif [[ "$URL" == *"bandcamp.com"* ]]; then
    PLATFORM="bandcamp"
elif [[ "$URL" == *"audiomack.com"* ]]; then
    PLATFORM="audiomack"
elif [[ "$URL" == *"mixcloud.com"* ]]; then
    PLATFORM="mixcloud"
elif [[ "$URL" == *"hearthis.at"* ]]; then
    PLATFORM="hearthisat"
elif [[ "$URL" == *"vocaroo.com"* || "$URL" == *"voca.ro"* ]]; then
    PLATFORM="vocaroo"
elif [[ "$URL" == *"instagram.com"* || "$URL" == *"instagr.am"* ]]; then
    PLATFORM="instagram"
elif [[ "$URL" == *"x.com"* || "$URL" == *"twitter.com"* ]]; then
    PLATFORM="x"
fi

if [[ "$PLATFORM" == "unknown" ]]; then
    echo -e "${YELLOW}Warning: URL does not appear to be one of the app's tested sources.${NC}"
fi

check_deps "$PLATFORM"

read -rp "Set custom metadata (title/artist/album/album artist)? [y/N] " meta_answer
META_TITLE=""
META_ARTIST=""
META_ALBUM=""
META_ALBUM_ARTIST=""
if [[ "$(echo "$meta_answer" | tr '[:upper:]' '[:lower:]')" == "y" ]]; then
    read -rp "  Title (leave blank to skip): " META_TITLE
    read -rp "  Artist (leave blank to skip): " META_ARTIST
    read -rp "  Album (leave blank to skip): " META_ALBUM
    read -rp "  Album artist (leave blank to skip): " META_ALBUM_ARTIST
fi

read -rp "Mark as explicit? [y/N] " explicit_answer
EXPLICIT="no"
[[ "$(echo "$explicit_answer" | tr '[:upper:]' '[:lower:]')" == "y" ]] && EXPLICIT="yes"

# Build a single AtomicParsley pass for any requested tag changes
ATOMIC_TAIL=""
[[ -n "$META_TITLE" ]]        && ATOMIC_TAIL+=" --title $(printf '%q' "$META_TITLE")"
[[ -n "$META_ARTIST" ]]       && ATOMIC_TAIL+=" --artist $(printf '%q' "$META_ARTIST")"
[[ -n "$META_ALBUM" ]]        && ATOMIC_TAIL+=" --album $(printf '%q' "$META_ALBUM")"
[[ -n "$META_ALBUM_ARTIST" ]] && ATOMIC_TAIL+=" --albumArtist $(printf '%q' "$META_ALBUM_ARTIST")"
[[ "$EXPLICIT" == "yes" ]]    && ATOMIC_TAIL+=" --advisory explicit"

EXPLICIT_ARGS=()
if [[ -n "$ATOMIC_TAIL" ]]; then
    EXPLICIT_ARGS=(--exec "AtomicParsley {}${ATOMIC_TAIL} --overWrite")
fi

echo -e "${GREEN}Downloading: $URL${NC}"
echo ""

# Playlists/carousels go into a named subfolder; single tracks stay flat.
if [[ "$PLATFORM" == "instagram" || "$PLATFORM" == "x" ]]; then
    OUTPUT_TMPL="%(title).100B.%(ext)s"
elif [[ "$URL" == *"/sets/"* ]]; then
    OUTPUT_TMPL="%(playlist_title)s/%(title)s.%(ext)s"
else
    OUTPUT_TMPL="%(title)s.%(ext)s"
fi

yt-dlp \
    --format "bestaudio[ext=m4a]/bestaudio[ext=aac]/bestaudio/best" \
    --extract-audio \
    --audio-format m4a \
    --audio-quality 0 \
    --embed-thumbnail \
    --embed-metadata \
    --embed-chapters \
    --add-metadata \
    "${EXPLICIT_ARGS[@]}" \
    --output "$OUTPUT_TMPL" \
    "$URL"

echo ""
echo -e "${GREEN}Done.${NC}"
