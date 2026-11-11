# SolidCraft reference import

`coglet-c0_6_8-reference.sc` was generated with SolidCraft CLI 0.30.1:

```powershell
./tools/solidcraft.ps1 run hardware/mechanical/original/CogletC0_6_8.3mf --save hardware/mechanical/solidcraft/coglet-c0_6_8-reference.sc --quiet
```

This is a **mesh reference in print layout**, not a reconstructed parametric
design or an assembled robot. Original designs: Will Cogley / NMRobotics LTD,
CC BY-NC 4.0; see [upstream license](../original/LICENSE.upstream) and
[provenance](../UPSTREAM.md). Conversion to `.sc` is the only change here.

## Verification on 2026-11-10

- Reopened the saved `.sc` and inspected it with the CLI.
- 48 imported bodies match the 48 top-level slicer objects. The source contains
  61 component occurrences: two compound objects contain six and nine parts.
  SolidCraft retains those groups, so body count is not individual part count.
- All 1,615,912 source triangles are represented in imported mesh face counts.
- All 48 imported bodies report closed. This is not a fit or printability test.
- Rendered and visually inspected [the reference view](../reference/solidcraft-reference.png).
- Stored CLI inspection in `../reference/solidcraft-inspection.json`.
  Its material/mass values are software assumptions, not measured hardware data.

## Next modeling work

### Individual reference bodies

`coglet-c0_6_8-individual.sc` separates the compound objects into 61 individually
named meshes. Reproduce it with `python tools/split_coglet_reference.py`, then
run the generated `reference/coglet-c0_6_8-individual.3mf` through the CLI with
`--save hardware/mechanical/solidcraft/coglet-c0_6_8-individual.sc`.
All 1,615,912 triangles and overall bounds match the original import; all
61 bodies report closed. Part-local vertices are unchanged, and component/build
transforms are composed to preserve print placement. This does not recover
assembled positions or parametric history. The same upstream license applies.

`python tools/render_coglet_parts.py` creates isolated views of the six parts
on plate one. See [structural close-ups](../reference/parts/STRUCTURAL-PARTS.md).
The renderer removes other bodies only from its temporary in-memory session;
the reference design is not saved over.

Map generic upstream names to functional roles, separate the compound groups
for individual reference access, and establish assembled datums before using
geometry to decide mounting clearances. Retain the original import unchanged.
Recreate editable mounting parts in separate designs with dimension provenance.

Before finalizing horn interfaces and carrier mounts, obtain the actual servo
model(s), horn style, breakout variants and dimensions of existing modifications.
Use annotated views to request specific measurements; no physical verification
has been performed during this import.
