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

The release zip carries the unmodified Windows embeddable package of Python
3.12.10 from [python.org](https://www.python.org/downloads/release/python-31210/)
in `python\`; one line (`..\tools`) is added to `python312._pth` so it finds the
tools. Python is Copyright the Python Software Foundation and distributed under
the PSF License Agreement; its complete license is `python\LICENSE.txt`.

The Blade & Sorcery mod is a mechanics reference. Its files are not bundled or
linked. The ArkWeb research checkout is likewise not bundled or linked. See
`docs/REFERENCE.md` for attribution and independently checked address facts.
