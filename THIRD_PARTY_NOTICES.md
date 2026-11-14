# Third-party notices

Spidy links the unmodified Khronos OpenXR loader and uses the OpenXR headers from
SDK release 1.1.49, commit `977f6675bc0057d5a54ed290cb5c71c699b1c0ab`.

- OpenXR SDK: Copyright The Khronos Group Inc.; Apache License 2.0.
  The complete license is in `docs/licenses/OpenXR-Apache-2.0.txt`.
  Per-file SPDX notices are preserved in `third_party/OpenXR-SDK`.
- The SDK's bundled JsonCpp: Copyright Baptiste Lepilleur and The JsonCpp Authors;
  public domain / MIT terms. Complete notice in `docs/licenses/JsonCpp.txt`.

`tools/bootstrap.ps1` obtains the pinned dependency from the official
[Khronos repository](https://github.com/KhronosGroup/OpenXR-SDK).
Retain these notices with a redistribution of the linked binaries.

The optional camera diagnostic DLL links unmodified MinHook 1.3.4, commit
`c3fcafdc10146beb5919319d0683e44e3c30d537`, from the
[author's repository](https://github.com/TsudaKageyu/minhook).
Its BSD license and the bundled Hacker Disassembler Engine notices are retained
in `docs/licenses/MinHook-LICENSE.txt`. Fetch it with `tools/bootstrap.ps1 -Observer`.

The game modules in the release zip (`tools/package.ps1`) link the same MinHook,
so its license ships with them.

The launcher (`Spidy Launcher.exe`, `apps/launcher`) is built with unmodified
Dear ImGui 1.92.9b, commit `f1cc2ae15e53a861a874c3034aae6798fde194ab`, from the
[author's repository](https://github.com/ocornut/imgui): Copyright Omar Cornut;
MIT license, retained in the release zip as `docs/licenses/DearImGui-LICENSE.txt`.
`tools/bootstrap.ps1` fetches it.

The launcher carries unmodified fonts from the
[Google Fonts repository](https://github.com/google/fonts) in
`apps/launcher/fonts`, compiled into `Spidy Launcher.exe`: Barlow Medium and
SemiBold, and Barlow Condensed SemiBold, Bold and Black Italic, Copyright 2017
The Barlow Project Authors (https://github.com/jpt/barlow); Comic Neue Bold
Italic, Copyright 2014 The Comic Neue Project Authors
(https://github.com/crozynski/comicneue). Both are licensed under the SIL Open
Font License 1.1, retained as `docs/licenses/Barlow-OFL.txt` and
`docs/licenses/ComicNeue-OFL.txt`.

Body haptics (`src/bhaptics_output.cpp`) load bHaptics' own library,
`bhaptics_library.dll`, from the bHaptics C/C++ SDK 2.7.1
([tact-cpp2](https://github.com/bhaptics/tact-cpp2), commit
`d016a3f22a4c1e5b6b2192e3ed2224512a045a19`), whose header `bhaptics.h` names
its functions. `bhaptics_library` and `bhaptics.h` are © bHaptics Inc. and
distributed under the [bHaptics SDK agreement](https://bhaptics.gitbook.io/license-sdk/),
not under Spidy's license. The release zip ships the library unmodified in
`build\windows-ninja\`, with the notices of the open-source components inside
it as `docs/licenses/bHaptics-THIRD_PARTY_NOTICES.txt`. `tools/bootstrap.ps1`
fetches the SDK and checks the library's SHA-256. The bHaptics Player is not
bundled: players install it from bHaptics. Spidy is an independent project, not
made, sponsored or endorsed by bHaptics.

The release zip carries the unmodified Windows embeddable package of Python
3.12.10 from [python.org](https://www.python.org/downloads/release/python-31210/)
in `python\`; one line (`..\tools`) is added to `python312._pth` so it finds the
tools. Python is Copyright the Python Software Foundation and distributed under
the PSF License Agreement; its complete license is `python\LICENSE.txt`.

The Blade & Sorcery mod is a mechanics reference. Its files are not bundled or
linked. The ArkWeb research checkout is likewise not bundled or linked. See
`docs/REFERENCE.md` for attribution and independently checked address facts.

The blocking sounds of Enemy types, in `include/spidy/block_sound.hpp` and
`include/spidy/web_block_sound.hpp` (used by `src/impact_sound.cpp`), are sound effects from
[Pixabay](https://pixabay.com/), cut, levelled and stored there as sample tables. They are used
under the [Pixabay Content License](https://pixabay.com/en/service/license/), not under Spidy's
license: free to use and change, no credit required, but they may not be sold or distributed on a
standalone basis.
