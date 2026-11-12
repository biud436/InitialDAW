# Licenses and third-party software

## What covers what

| Part | License |
| :--- | :--- |
| Source code in this repository (`engine/`, `app/`, `build.sh`, `tools/`, tests, schemas, docs except the song notes) | GNU Affero General Public License version 3 (AGPL-3.0-only), Copyright (C) 2026 biud436, see [LICENSE](LICENSE) |
| Built binaries (`InitialDAW.app`, `music-render`) | AGPLv3, the same licence as the source and as JUCE ([LICENSE](LICENSE)) |

The repository itself contains no third-party code. JUCE, sfizz, LAME, Signalsmith Stretch and Linear, the Bravura font and the sample libraries are downloaded into `external/` by
`build.sh` and are never committed.

## Libraries linked into the binaries

### JUCE 8.0.10

Dual licensed: [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) or the
[JUCE commercial licence](https://juce.com/legal/juce-8-licence/) (the free Starter tier covers up to USD 20,000 of
annual revenue or funding). This repository's code follows JUCE's open source option and is licensed under the AGPLv3
as well, so a binary that includes it is distributed under the AGPLv3 whichever JUCE licence the distributor holds.
Pinned by tag in `build.sh`.

Components bundled with JUCE that this build uses:

| Component | License | Used for |
| :--- | :--- | :--- |
| VST3 SDK (Steinberg) | Proprietary Steinberg VST3 License or GPLv3 (the GPLv3 option is compatible with AGPLv3) | hosting VST3 instruments, the test plugin |
| FLAC | BSD | reading and writing FLAC files |
| Ogg Vorbis | BSD | reading and writing Ogg Vorbis files |
| libpng | zlib/libpng | snapshot PNGs |
| jpeglib | Independent JPEG Group License | image decoding in juce_graphics |

Audio Unit hosting uses Apple's system frameworks. LV2, LADSPA, OpenGL, JavaScript and Box2D modules are not built.

### sfizz

[BSD 2-Clause](https://github.com/sfztools/sfizz/blob/develop/LICENSE), pinned to commit
`f5c6e29f23b8057867c08e88f5f6ac6738baa30b` in `build.sh`. Libraries it compiles into the static library:

| Library | License |
| :--- | :--- |
| abseil-cpp | Apache 2.0 |
| atomic_queue | MIT |
| cephes | BSD (permission from the author, see its LICENSE.txt) |
| cpuid | BSD 3-Clause |
| ghc filesystem | MIT |
| hiir | WTFPL |
| invoke.hpp | MIT |
| jsl | Boost Software License 1.0 |
| kiss_fft | BSD 3-Clause |
| pugixml | MIT |
| simde | MIT |
| spline | BSD 3-Clause |
| st_audiofile | BSD 2-Clause, with dr_libs (public domain or MIT-0), libaiff (MIT-style), stb_vorbis (MIT or public domain), WavPack (BSD 3-Clause) |
| threadpool | zlib |
| tunings | MIT |

### LAME 4.0

[LGPL-2.0](https://lame.sourceforge.io/license.txt) (GNU Library General Public License, version 2 or later), the MP3 encoder
used by MP3 export. `build.sh` downloads the release tarball from SourceForge, checks its SHA-256
(`3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb`) and builds only the encoder library
(`libmp3lame.a`, no frontend, no decoder) without changes. It is linked statically. The LGPL allows this in a
program under the AGPLv3 as long as users can relink it with a modified LAME: the complete source of every binary is
published with the release (this repository's tag plus the pinned LAME tarball). Without `external/lame` the build
leaves MP3 export out and the other formats still work.

### Signalsmith Stretch 1.4.0 and Signalsmith Linear 0.6.4

[MIT](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/main/LICENSE.txt), Copyright 2022 Geraint Luff / Signalsmith Audio Ltd.
(Linear: Copyright 2025 Signalsmith Audio). Header-only C++ that stretches audio clips in time and changes their pitch.
`build.sh` clones the tags `1.4.0` (commit `a670068d9aeb64913331d5cc29337b19a457a7df`) and `0.6.4`
(commit `de55e6a50ffcf6f8f43f649692d94691c7025151`, the version Stretch 1.4.0 pins) and checks the commits. Without them the
build still works and stretched clips play unstretched with a warning.

### libFLAC 1.4.3, libogg 1.3.5 and libvorbis 1.3.7

[BSD-3-Clause](https://github.com/xiph/flac/blob/master/COPYING.Xiph) (Xiph.Org), the FLAC and Ogg Vorbis encoders and decoders
of the engine (export and reading audio files). `build.sh` downloads the release tarballs (`flac-1.4.3.tar.xz` from the
GitHub release, SHA-256 `6c58e69cd22348f441b861092b825e591d0b822e106de6eb0ee4d05d27205b70`; `libogg-1.3.5.tar.xz`,
`c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705`, and `libvorbis-1.3.7.tar.xz`,
`b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b`, from downloads.xiph.org) and CMake builds the libraries
from them without changes (only libFLAC's library sources, not its programs or the GPL-licensed tools). They are the same
versions JUCE bundles, so the encoded bytes match earlier exports.

## Bundled in the app

### Bravura 1.482

[SIL Open Font License 1.1](https://openfontlicense.org/), Copyright 2015 Steinberg Media Technologies GmbH, with Reserved Font
Name "Bravura". The SMuFL music font that draws the score view. `build.sh` downloads `Bravura.otf` and `OFL.txt` from the
[bravura-1.482 release](https://github.com/steinbergmedia/bravura/releases/tag/bravura-1.482) and checks their SHA-256
(`cdf0f893ee1fdb64b7f6713d71ee0dcfc349c0ac01429a8e451b01a9e79f5f3b` and `c24929be7028026a65ee8894da1e3c36d2a4ccce0548d9ba8ba64509f46319ee`).
The unmodified font is embedded in the app as binary data; the OFL allows bundling it with software as long as its licence
and copyright travel with it and it is not sold on its own. Without `external/bravura` the app builds and the score view
says the font is missing.

## Not included anywhere

| Item | License | Notes |
| :--- | :--- | :--- |
| [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) | CC0 | sample library, fetched by `./build.sh --samples` |
| [Sonatina Symphonic Orchestra](https://github.com/peastman/sso), Mattias Westlund | CC Sampling Plus 1.0 | sample library, fetched by `./build.sh --samples`. Music made with it must credit it, and may only be advertised or promoted as that music itself. Its Grand Piano (the Splendid Grand Piano, recorded by Akai) is public domain |
| [FreePats Spanish classical guitar](https://github.com/freepats/spanish-classical-guitar) v1.0.0 | CC0 1.0 | sample library (the `guitar` instrument), fetched by `./build.sh --samples` at a pinned commit |
| [FreePats MuldjordKit](https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html) 2020-10-18, Lars Muldjord | CC BY 4.0 | acoustic drum kit (the `drum-kit` instrument), fetched by `./build.sh --samples` at a pinned commit. Music or anything else made with it must credit "MuldjordKit by Lars Muldjord (FreePats version), CC BY 4.0" |
| [FreePats electric bass YR](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-bass.html) 2019-09-30 | CC0 1.0 | sample library (the `bass-guitar` instrument), fetched by `./build.sh --samples` at a pinned commit |
| [FreePats FSBS electric guitars](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html) 2026-08-07, clean and jazz | CC0 1.0 | sample libraries (the `electric-guitar` and `jazz-guitar` instruments), fetched by `./build.sh --samples` at pinned commits |
| [FreePats tenor saxophone](https://freepats.zenvoid.org/Reed/saxophone.html) 2020-07-17 (from the Versilian Community Sample Library) | CC0 1.0 | sample library (the `tenor-sax` instrument), fetched by `./build.sh --samples` with a pinned SHA-256 |
| [Karoryfer x bigcat cello](https://github.com/sfzinstruments/karoryfer-bigcat.cello) v1.001, Copyright 2016 Karoryfer Lecolds and bigcat instruments, played by Kamila Borowiak | CC0 1.0 | sample library (the `cello-solo` instrument), fetched by `./build.sh --samples` with a pinned SHA-256 into `external/karoryfer`. Its own SFZ and Kontakt programs are not used: `tools/make-cello-sfz.py` (AGPLv3, this repository) writes our SFZ next to the samples. CC0 asks for no credit; we credit it anyway |
| 국립국악원 (National Gugak Center), [「국악기 디지털 음원」](https://www.gugak.go.kr/digitaleum/): gayageum single notes (player 윤다영), haegeum scales, janggu strokes, ajaeng notes and jing strokes | 공공누리 제1유형 (KOGL Type 1, attribution) | 42 gayageum notes in three dynamics, three haegeum scales, 18 janggu strokes, 27 long ajaeng notes with tremolo, sul ponticello and effect recordings, and two jing strokes (the `gayageum`, `haegeum`, `janggu`, `ajaeng` and `jing` instruments), fetched by `./build.sh --gugak` into `external/gugak/ngc` with each file's SHA-256 pinned. The site asks for a usage form with every download (commercial or not, purpose, affiliation); the script sends the values the person downloading gives it and sends nothing without them. `tools/make-gugak-sfz.py` (AGPLv3, this repository) writes the SFZ. Music made with them must credit the source, for example "본 저작물은 국립국악원이 공공누리 제1유형으로 개방한 「국악기 디지털 음원」을 이용하였습니다 (https://www.gugak.go.kr/digitaleum/)" |
| 국립국악원 (National Gugak Center), 「국악기 디지털 음원」 산조대금 scale recording, via [jonijonii/Daegeum-Sanjo-Sampler](https://github.com/jonijonii/Daegeum-Sanjo-Sampler) (code MIT) | 공공누리 제1유형 (KOGL Type 1, attribution) | 18 daegeum notes sliced from the National Gugak Center recording (the `daegeum` instrument), fetched by `./build.sh --gugak` from a pinned commit with each file's SHA-256 pinned into `external/gugak/daegeum`. Music made with it must credit the source, for example "본 저작물은 국립국악원이 공공누리 제1유형으로 개방한 「국악기 디지털 음원」을 이용하였습니다 (https://www.gugak.go.kr/digitaleum/)" |
| [Dexed](https://github.com/asb2m10/dexed) 1.0.1 | GPLv3 | optional free VSTi (the `dexed` instrument), fetched by `./build.sh --plugins` into `external/plugins/dexed` with a pinned SHA-256; loaded at run time as a separate plugin, never linked or bundled |
| System fonts (Apple SD Gothic Neo, Menlo) | Apple | used from the system, not distributed |

## Releasing binaries

1. Build from a tagged commit so the source of every release can be named exactly (this repository's tag, JUCE tag 8.0.10,
   sfizz commit above, LAME 4.0 tarball and Bravura 1.482 with the SHA-256 above, Signalsmith Stretch and Linear commits above).
2. Put these next to the binaries in the release archive:
   - `LICENSE` (the AGPLv3 text) and `THIRD-PARTY.md`
   - the licence texts of JUCE, sfizz and its libraries, LAME, Signalsmith Stretch and Linear, and Bravura (OFL), collected with `tools/collect-licenses.sh <folder>`
   - the LAME source tarball (`external/lame/lame-4.0.tar.gz`) or a link to it, for the LGPL
   - a note that the binaries are distributed under the AGPLv3, with a link to the source of that tag
3. Do not put the sample libraries or anything from `songs/` into a release archive.
