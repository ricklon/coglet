# Builder readiness review — 2026-11-10

Coglet has a useful electronics and firmware foundation, but the repository
does not yet describe a reproducible complete mechanical build.

## Strengths

- One pad map across boards, explicit power separation and released boot.
- Progressive one-servo fitting/calibration and a qualification record that
  distinguishes compiled code from observed physical behavior.
- Carrier instructions generated from one Python source: hole coordinates,
  mirrored underside labels, one link per step and connectivity checks.
- Browser wiring/flashing workflow and machine-readable wiring/BOM data.

## Gaps, ordered by effect on a new builder

1. **Modified mechanical CAD is missing.** The initial review found no local
   mechanical assets. The owner subsequently selected upstream's latest C0_6_8
   baseline; its STL/3MF and license are now downloaded with verified hashes.
   Non-TorqueStar adaptation remains unverified.
   See [the source inventory](../hardware/mechanical/UPSTREAM.md).
2. **Mechanical assembly is unfinished.** All nine role mounting entries in
   ASSEMBLY.md are TODOs. Carrier mounting, microphone direction and vertical
   clearance are explicitly pending in CARRIER.md.
3. **Carrier remains a design.** Breakout pin orders and wire lengths are
   assumptions. Its documentation does not establish a physically built unit.
4. **BOM cannot specify a complete purchase.** Servo model, supply current,
   printed parts, screws, horns, linkages and carrier mounting hardware need
   exact specifications. The carrier board itself is missing from bom.csv.
5. **Readiness language needs tightening.** README's support matrix can look
   stronger than the physical qualification record; label implemented, built
   and physically verified separately. The old Fritzing schematic and some
   roadmap tasks also need reconciliation with current project state.
6. **Licensing resolved after review.** The owner selected CC BY-NC 4.0;
   root LICENSE and NOTICE.md now document the project terms, original mechanical
   attribution and separately licensed dependencies.

## How to express the build

Keep `tools/carrier/carrier.py` as the source for electrical carrier steps.
Extend its structured steps when bench evidence changes placement or wiring,
then regenerate Markdown, HTML and SVG together. Installation needs its own
mechanical sequence linked from ASSEMBLY.md; it must use the same part IDs,
revisions and views as the CAD assembly.

Each instruction should contain:

| Field | What the builder sees |
|---|---|
| Prerequisites | Completed stage, exact part revisions and power state |
| Parts/tools | IDs, quantities, fastener dimensions and tools |
| View | Consistent orientation, highlighted new part, underside label if needed |
| Action | One operation with endpoints or measured dimensions |
| Check | Observable result, meter points or fit criterion |
| If it fails | Where to stop and what to inspect |
| Evidence | Design-only drawing, or dated photo of the actual assembly |

Recommended chapter sequence:

1. Inventory and print parts; identify board variants and pin orders.
2. Prepare headers and carrier; solder one link at a time using the existing
   carrier guide and perform its continuity/isolation checks.
3. Complete electronics bench checks with servo power off.
4. Dry-fit the carrier and electronics inside the open Coglet shell.
5. Apply documented modifications: each needs a before/after view, datum,
   dimensions, tool, debris cleanup and fit check. The exact modifications
   remain undecided until the original geometry and measurements are available.
6. Install insulation, mounts, boards, speaker/mic and cable strain relief;
   retain USB access and a reachable servo supply switch.
7. Fit mechanisms base upward; route wires clear of moving joints; calibrate
   each servo using the existing procedure.
8. Close the shell, repeat clearance checks and complete qualification.

For people with existing prints, publish a dimensioned modification route;
for new builders, publish revised print files that incorporate those changes.
Both routes must reach the same documented assembly revision.

## Checks run during this review

- SolidCraft 0.30.1: project launcher version check, MCP initialization and
  `tools/list` passed. Client registration and robot reconstruction are not done.
- Wiring consistency: passed, 20 wires match.
- Carrier `--check`: the initial 13-file freshness failure was traced to Windows
  default encoding, not stale diagrams. Explicit UTF-8 reads match every generated
  file. The generator now reads/writes UTF-8 explicitly; regenerated and rechecked.
- Diff whitespace check: passed. No firmware or physical tests were run.

## Next acceptance milestone

A builder should be able to download a revisioned parts pack, buy an exact BOM,
print and assemble the mechanism, construct and install the carrier, and run
the qualification sequence without privately supplied instructions. Validate
that with a second builder, recording questions and corrections.

The upstream C0_6_8 baseline is selected and downloaded. The immediate
dependencies are horn-interface inspection and measurements of modifications.
Our backend is agent-hub; upstream XiaoZhi enrollment instructions do not apply.
See
[mechanical source contract](../hardware/mechanical/README.md) and
[SolidCraft CLI/MCP setup](SOLIDCRAFT.md).
