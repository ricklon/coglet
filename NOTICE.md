# License scope and attribution

Unless otherwise marked, this project's original code, documentation, hardware
designs, CAD, generated designs and illustrations are licensed under
**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.
See [LICENSE](LICENSE) for the full terms and the
[license summary](https://creativecommons.org/licenses/by-nc/4.0/).

Credit the Coglet project contributors and link to
<https://github.com/ricklon/coglet> when sharing this project's material.
Retain existing author/copyright notices and indicate modifications.

## Original Coglet mechanical design

Original design: **Will Cogley / NMRobotics LTD**, copyright 2026.
Source: <https://github.com/will-cogley/Coglet>.
Upstream hardware designs and 3D models are CC BY-NC 4.0 unless otherwise noted.
The preserved [upstream license](hardware/mechanical/original/LICENSE.upstream)
and [pinned source inventory](hardware/mechanical/UPSTREAM.md) remain applicable.

The original C0_6_8 STL/3MF files are unchanged. Derived reference files convert
the meshes to SolidCraft, separate and name components, and render part views;
their documentation records those changes. Preserve upstream attribution with
those derivatives. The servo diameter coupon is newly constructed project
geometry and uses the project's CC BY-NC 4.0 license.

## Third-party material and dependencies

The project license does not replace separately licensed third-party material,
dependencies, vendored assets or submodules. Their licenses and notices govern
that material; preserve them when redistributing it.

- `firmware/xiaozhi-esp32`: separate firmware submodule, with its own MIT
  license and notices for its dependencies.
- Espressif `esp_audio_codec`: Espressif Modified MIT license, including its
  Espressif-chip restriction.
- Other third-party code and assets: retain the notices supplied with those
  files and packages. This list is not an exhaustive dependency audit.

Project tooling such as SolidCraft retains its own software license; using it
does not replace the licenses of Coglet source geometry or generated artifacts.
