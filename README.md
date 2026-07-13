# hoyo voice extractor

Extract and annotate HoYoverse game voice audio on macOS, Linux, and Windows.

## Native tools

The TypeScript API uses two native, multithreaded batch tools:

- [`hoyo-pck-extract`](quickpck/README.md) extracts Wwise AKPK `.pck` archives.
  It accepts either a single archive or a directory, recursively processes all
  discovered PCK files with a worker pool, and writes their `.bnk` and `.wem`
  entries into a matching output directory structure.
- [`hoyo-audio-convert`](wwise/README.md) converts Wwise audio to WAV. It
  recursively decodes standalone `.wem` files and extracts embedded WEM entries
  from `.bnk` files in-process, then decodes them in parallel while preserving
  the relative input paths.

Together, the tools cover the native audio path from game PCK archives to WAV
files ready for annotation by the TypeScript API. See their linked READMEs for
build options, command-line usage, supported codecs, and licensing details.

Basic usage:

```bash
# Extract one PCK file or every PCK below a directory
./quickpck/build/hoyo-pck-extract <input-pck-or-directory> <output-directory>

# Convert one WEM/BNK file or every WEM/BNK below a directory to WAV
./wwise/build/hoyo-audio-convert <input-wem-bnk-or-directory> <output-directory>
```

Both commands accept `--threads N` to set the worker count. Directory inputs
are processed recursively, so the output from `hoyo-pck-extract` can be passed
directly to `hoyo-audio-convert`.

Build both tools before running a game pipeline:

```bash
./quickpck/build.sh
./wwise/build.sh
npm run build
```

On Windows, run `quickpck/build.ps1` and `wwise/build.ps1` instead. The tool
paths can be overridden with `HOYO_PCK_EXTRACT` and `HOYO_AUDIO_CONVERT`.
