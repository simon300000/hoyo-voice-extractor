# hoyo voice extractor

Extract and annotate HoYoverse game voice audio on macOS, Linux, and Windows.

The TypeScript API uses two native batch tools:

- `quickpck/` builds `hoyo-pck-extract` for recursive, multithreaded PCK extraction.
- `wwise/` builds `hoyo-audio-convert` for recursive, multithreaded BNK/WEM decoding to WAV.

Build both tools before running a game pipeline:

```bash
./quickpck/build.sh
./wwise/build.sh
npm run build
```

On Windows, run `quickpck/build.ps1` and `wwise/build.ps1` instead. The tool
paths can be overridden with `HOYO_PCK_EXTRACT` and `HOYO_AUDIO_CONVERT`.
