# Freelancer Decomp

A reverse-engineered reimplementation of the **Freelancer** (2003) game client and its engine
DLLs, built from source with a modern Visual Studio. Every module the game loads is built from
`src/`; no retail binary of the game is loaded except the two third-party DLLs listed below.

You need your own copy of Freelancer: the game data (`DATA`) is not part of this repository.

Documentation: [docs/README.md](docs/README.md).

## Building and running

1. **Clone** this repository.
2. **Add the game data:** copy the `DATA` folder of an installed Freelancer (or of a mod) into
   the repository root, next to `src`, so that `DATA\` and `DATA_EXTRA\` sit side by side.
3. **Build the Release configuration** of `src\Freelancer.sln` (`Release|Win32`) in Visual Studio,
   or from a Developer Command Prompt (no legacy DirectX 8/9 SDK required — uses the Windows SDK only):

   ```
   msbuild src\Freelancer.sln -t:Build -p:Configuration=Release -p:Platform=Win32
   ```

The output goes to `bin\`, which already holds the runtime configuration (`DACOM.INI`,
`freelancer.ini`, `FLConfigDatabase.txt`, `NewPlayer.fl`, `MPNewCharacter.fl`). Run
`bin\freelancer.exe` **from the `bin` directory** (the library and data paths are relative to it).

### Requirements

- Visual Studio 2026 (platform toolset v145) with the *Desktop development with C++* workload,
  the *C++ MFC for latest v145 build tools (x86 & x64)* component (used by `FLServer`) and a
  Windows 10/11 SDK. Everything builds as 32-bit (`Win32`).
- Debug builds (`bin_debug\`) also need Python 3 on `PATH`: a post-build step copies the runtime
  configuration from `bin\`.

### Optional features

Port-only features are build switches in `src/Freelancer.Diagnostics.props` (also on the VS
property page *Freelancer diagnostics*), or `/p:<Name>=0|1` on the MSBuild command line:

| Property | Default | What it does |
|---|---|---|
| `FreelancerWidescreenSupport` | 1 | Widescreen (Hor+) display modes and HUD layout |
| `FreelancerRp9` | 1 | Client Direct3D 9 extension hooks such as volumetric nebulae (`RP9.dll` is always built using the Windows SDK only; [docs/re/rp9.md](docs/re/rp9.md)) |
| `FreelancerBetaLabel` | 0 | The "Beta" label in the corner of the screen |
| `FreelancerEnableTickProfile` | 0 | On-screen frame profiler |

## Repository layout

```
src\          Visual Studio solution (Freelancer.sln) — all source code
bin\          Release output + the runtime configuration files
DATA_EXTRA\   the port's own data (RP9 shaders, volumetric.ini), next to the game's DATA
EBUEula.dll   retail EULA dialog, loaded as ..\EBUEula.dll with ..\EULA.rtf (as the retail game)
EXE\          gundll.dll — the retail GUN client (multiplayer server list), loaded as ..\exe\gundll.dll
docs\         documentation
```

`EBUEula.dll` and `gundll.dll` are the only retail binaries the port uses unchanged: they are
third-party components of the original game and have not been reimplemented.

## License

The source code of Freelancer Decomp is released into the public domain under
[The Unlicense](LICENSE). This does not cover third-party code and files in the repository:
the vendored libraries under `src\` (`src\zlib\vendor`, `src\qhull\vendor`) keep their own
licenses, and the retail files (`EBUEula.dll`, `EULA.RTF`, `EXE\gundll.dll`) and the game data
remain the property of their respective owners.

## Support the project

If you would like to support the work on Freelancer Decomp, donations are welcome:

| Currency (network) | Address |
|---|---|
| USDT (BEP20) | `0x3bedb75137d850b8f24a42f488459a6a94489105` |
| USDT (TRC20) | `TZA2s2A7m8rCuXgDsMJs7XhyoZmX7BBc6j` |
| Bitcoin (BTC) | `1JooBx2a86hNhqdU7uYiKAfxaHVHBqD1wV` |
| ETH (ERC20) | `0x3bedb75137d850b8f24a42f488459a6a94489105` |

Send only the currency listed on a line, over the network listed next to it.

## Contacts

- Discord: `@Giperion`
- Email: [gipdereline@gmail.com](mailto:gipdereline@gmail.com)
