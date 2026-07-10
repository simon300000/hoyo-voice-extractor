# hoyo-pck-extract

`hoyo-pck-extract` recursively extracts Wwise AKPK `.pck` archives. It replaces
the project-specific QuickBMS invocation:

```bash
quickbms -q -k wwise_pck_extractor.bms input.pck output
```

but runs once for a whole directory and distributes archives across worker
threads.

## License

GPL-2.0-or-later. See `LICENSE`.

## Build

```bash
./build.sh
```

On Windows:

```powershell
.\build.ps1
```

## Usage

```bash
hoyo-pck-extract <input-pck-or-directory> <output-directory> [--threads N]
```

Each archive is extracted below `<output-directory>/<relative-pck-path-without-extension>/`.
For example, `AudioAssets/en/voice.pck` writes files below
`result/wem/en/voice/`.

Output extensions follow the original BMS script:

- `.bnk` for bank entries
- `.wem` for sound and external entries
- legacy `.xma`, `.ogg`, or `.wav` for old Wwise banks when the codec can be
  inferred
