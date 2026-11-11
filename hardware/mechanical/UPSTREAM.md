# Original Coglet sources

Author: Will Cogley / NMRobotics LTD.
Repository: <https://github.com/will-cogley/Coglet>.
Inventory checked 2026-11-10 at commit
`993a556a25753eb1f4379d18750e8a9cf1e92c7d` (complete recursive tree).

## Our build variant

On 2026-11-10 the owner selected **the latest upstream print set, C0_6_8**, as
the reconstruction baseline, pinned to the commit above. The earlier requirement
to build **without TorqueStars** remains: inspect and adapt the servo/horn
interfaces rather than assuming this pack already fits that requirement.
The backend is **agent-hub**.
Our firmware uses the xiaozhi wire protocol to communicate with agent-hub;
the S3 currently also uses a xiaozhi-esp32 firmware fork. Neither means builders
should enroll in the upstream XiaoZhi service or install upstream's backend.
Use this repository's wiring, firmware, hub setup and calibration instructions.

## Located assets

The selected STL and 3MF are now preserved unchanged in [original/](original/),
with `LICENSE.upstream` and a `manifest.json` containing SHA-256 and verified
upstream Git blob hashes. Archive/STL structural checks passed; geometry and
physical fit have not been verified.

| File | Location/revision | Status |
|---|---|---|
| `CogletC0_6_8.3mf` | [Pinned print directory](https://github.com/will-cogley/Coglet/tree/993a556a25753eb1f4379d18750e8a9cf1e92c7d/3D%20Printing%20Files) | Selected baseline; downloaded |
| `CogletC0_6_8_Generic.stl` | Same directory | Selected mesh reference; downloaded |
| `Production TorqueStar - TorqueStarV1.stl` | Same directory | Excluded from our build |
| `CogletB32Parts.3mf` | [Historical print directory](https://github.com/will-cogley/Coglet/tree/c82b47cdd6aafd4f77f4e221b88d138dae7a714f/3D%20Printing%20Files) | Older candidate, not confirmed as the owner's non-TorqueStar version |

Git blob IDs for provenance (these are Git blob hashes, not file SHA-256):

- C0_6_8 3MF: `501291f170e875fd46759ff13c393024964d7419`
- C0_6_8 STL: `71530a6657de6438e68638ac0e0c3b7096f63aec`
- TorqueStar STL: `b7f90b027d9406c207f76a8e4099ec58dd195ee7`
- B32 3MF: `62bd18af5e787ea03c65fa25b7a644830de275cc`

The inspected current tree contains no STEP, Fusion, FreeCAD, OpenSCAD or
SolidCraft mechanical source. STL/3MF availability does not establish editable
parametric CAD availability. No CAD of this project's carrier/enclosure
modifications has been found locally. Historical print packs need inspection
before identifying which one uses the desired horn attachment.

## Attribution and license

The [upstream license](https://github.com/will-cogley/Coglet/blob/993a556a25753eb1f4379d18750e8a9cf1e92c7d/LICENSE)
states CC BY-NC 4.0 for hardware designs, 3D models and original code unless
otherwise noted, with separate treatment for specified third-party code.
Preserve the upstream license and attribution with recovered geometry and
identify modifications. Do not label these assets with this project's code
license or SolidCraft's software license.

## What is needed before reconstruction

1. Inspect the selected C0_6_8 pack's horn interfaces and identify the actual
   servos/horns needed for the non-TorqueStar adaptation.
2. Keep the downloaded originals unchanged; model modifications separately.
3. Inventory its individual parts and compare them with the fitted robot.
4. Record existing cuts, holes, mounts and substitutions with measurements.
5. Recreate editable SolidCraft parts and add the carrier installation changes;
   keep source references, assumptions and fit-check results per part.

Upstream's [README](https://github.com/will-cogley/Coglet/blob/993a556a25753eb1f4379d18750e8a9cf1e92c7d/README.md)
links a build video and knowledge base. They can inform mechanical assembly,
but their electronics and backend instructions are not our build procedure.
