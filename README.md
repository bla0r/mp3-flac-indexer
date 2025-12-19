# mp3flac-indexer (C++20 + TagLib)

A small indexer that scans your music folders for **MP3** and/or **FLAC** files, reads tags via **TagLib**, and creates directory-based **symlink indexes**.

## Output layout

Given `INDEX_ROOT=/index`:

### MP3
- `/index/mp3/alpha/<A-Z|0-9|#>/<release>`
- `/index/mp3/genre/<genre>/<release>`
- `/index/mp3/year/<year>/<release>`
- `/index/mp3/groups/<group>/<release>`

Optional (if enabled in config):
- `/index/mp3/artist/<artist>/<release>`
- `/index/mp3/album/<album>/<release>`

### FLAC
- `/index/flac/alpha/<A-Z|0-9|#>/<release>`
- `/index/flac/genre/<genre>/<release>`
- `/index/flac/groups/<group>/<release>`
- `/index/flac/year/<year>/<release>`

Optional (if enabled in config):
- `/index/flac/artist/<artist>/<release>`
- `/index/flac/album/<album>/<release>`

`<release>` is the **release directory name**.

By default, the tool treats the **parent directory of the matched audio file** as the release.
For layouts that include a dated directory (e.g. `/site/recent/mp3/YYYY-MM-DD/<release>/...`),
use `MP3_RELEASE_DEPTH=2` / `FLAC_RELEASE_DEPTH=2` so the release directory is resolved as:
`<scan_root>/<YYYY-MM-DD>/<release>` even if tracks are nested deeper (`CD1/`, etc.).

Group is derived from the release directory name: substring after the last `-`.

## Build

### Ubuntu/Debian

Install TagLib:

```bash
sudo apt-get install -y libtag1-dev
```

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

Binary:

```bash
./build/mp3flac-indexer config.sample
```

## Config

See `config.sample`.

Important keys:
- `MP3_DIR=` (repeatable, preferred)
- `FLAC_DIR=` (repeatable, preferred)
- `MUSIC_DIR=` (repeatable fallback for both types)
- `INDEX_ROOT=`
- `ENABLE_TYPES=mp3,flac`
- `MP3_INDEXES=` and `FLAC_INDEXES=`
 - `MP3_RELEASE_DEPTH=` and `FLAC_RELEASE_DEPTH=`
 - `MP3_RELEASE_DEPTH=` and `FLAC_RELEASE_DEPTH=`

Supported index names:
`alpha`, `genre`, `year`, `groups`, `artist`, `album`
- `RELATIVE_SYMLINKS=true|false`
- `CLEAN_ON_START=true|false`

## Flags

- `--dry-run` : do not write anything
- `--force`   : replace existing links
- `--clean`   : clean enabled categories before indexing
- `--no-clean`: override config and do not clean

