# hoyo-audio-convert

`hoyo-audio-convert` recursively converts Wwise audio inputs to WAV:

- `.wem` files are decoded directly through libvgmstream.
- `.bnk` files are parsed in-process using DIDX/DATA extraction,
  then each embedded WEM is decoded to WAV.

This replaces the previous multi-process Wwise extraction flow with one batch
executable and a worker pool.

## License

The wrapper code in this folder is MIT licensed. It uses:

- libvgmstream, available under an ISC-style permissive license
- libogg and libvorbis, available under BSD-style permissive licenses

See `LICENSE`, `source/vgmstream/COPYING`, `source/ogg/COPYING`, and
`source/vorbis/COPYING`.

## Build

```bash
./build.sh
```

On Windows:

```powershell
.\build.ps1
```

The `source/` directory is a curated decoder-only snapshot of vgmstream,
libogg, and libvorbis. The build intentionally excludes upstream CLIs,
plugins, tests, packaging files, prebuilt libraries, and non-Wwise format
parsers. Supported Wwise codecs are PCM16, Wwise/Xbox IMA, Vorbis, DSP,
HEVAG, and Platinum ADPCM.

## Usage

```bash
hoyo-audio-convert <input-wem-bnk-or-directory> <output-directory> [--threads N]
```

Output paths preserve the relative input tree:

- `input/a/b.wem` -> `output/a/b.wav`
- `input/a/b.bnk` entry `123.wem` -> `output/a/b/123.wav`
