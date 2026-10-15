# Dependency notices

Opentone application source is licensed under GNU AGPL version 3 only; see LICENSE.
Copyright (c) 2026 Opentone contributors.

JUCE 8.0.15, Copyright (c) Raw Material Software Limited, is used under its AGPLv3 option.
The exact source revision is 91ad83ae34a81e0833b1a2b0866f54846370ae53:
https://github.com/juce-framework/JUCE/tree/91ad83ae34a81e0833b1a2b0866f54846370ae53
The upstream notice is reproduced in licenses/JUCE.md. Its relative links refer to
that JUCE source tree. The complete license texts of bundled dependencies are
reproduced in licenses/ and accompany evaluation builds.

Ableton Link 3.1.5 (revision 45931b8bd2a7066f1950f2a0beab81a0c51f28fe), Copyright
(c) Ableton AG, is used under its GPLv2-or-later license. Source:
https://github.com/Ableton/link/tree/45931b8bd2a7066f1950f2a0beab81a0c51f28fe.
Its notices are in licenses/Ableton-Link.md and licenses/Ableton-Link-GPL-2.txt.
Link's standalone Asio dependency, Copyright (c) Christopher M. Kohlhoff, uses
the Boost Software License 1.0 (licenses/Asio-Standalone.txt).

Selected JUCE modules include code from:

- FLAC: Xiph.Org Foundation and contributors, BSD license (licenses/FLAC.txt).
- Ogg Vorbis: Xiph.Org Foundation and contributors, BSD license (licenses/Ogg-Vorbis.txt).
- JPEG: Independent JPEG Group; this software is based in part on the work of the
  Independent JPEG Group (licenses/JPEG.txt).
- PNG: libpng contributors, libpng license (licenses/PNG.txt).
- zlib: Jean-loup Gailly and Mark Adler, zlib license (licenses/zlib.txt).
- HarfBuzz: its authors and contributors, MIT-style license (licenses/HarfBuzz.txt).
- SheenBidi: Muhammad Sheeraz and contributors, Apache 2.0 (licenses/SheenBidi.txt).
- ASIO SDK: Steinberg Media Technologies, GPLv3 option where included by JUCE's
  Windows audio module (licenses/ASIO.txt).
- LV2 SDK: its authors and contributors, ISC (licenses/LV2.txt).
- VST3 SDK: Steinberg Media Technologies, MIT (licenses/VST3.txt).

Opentone hosts Audio Units on macOS and VST3 plug-ins through JUCE's processor
modules. VST2 hosting is disabled; no VST2 SDK is bundled. JUCE's upstream notice lists additional
platform or unselected dependencies; Android, plugin-client and JavaScript modules
are not selected by this project. System libraries and OS audio/graphics frameworks
remain platform dependencies; Linux packages are listed in README.md.

No external audio recordings, fonts or artwork are bundled. The Opentone icon is original artwork in assets/. Check fixtures are
synthesized locally. The interface uses original styling inspired by desktop
arrangement editors; Ableton assets and source are not used.
