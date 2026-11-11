# Coglet mechanical sources

Original C0_6_8 STL/3MF files and their license are preserved in `original/`,
with verified hashes in `original/manifest.json`. Modified CAD has not yet been
created. These are upstream reference prints, not a qualified modified build.

The [illustrated inventory](reference/INVENTORY.md) lists 61 component
occurrences across five plates, including alternate skins. A verified
[SolidCraft mesh reference](solidcraft/README.md) is available; editable
parametric reconstruction and assembly placement remain to be done.

The original upstream is now identified as **will-cogley/Coglet**. Printable
The owner selected its latest C0_6_8 print set as the reconstruction baseline.
Non-TorqueStar interfaces still need checking/adaptation.
See [the pinned upstream inventory](UPSTREAM.md).
Our backend is agent-hub. SolidCraft means tangency-labs/solidcraft.

## Asset contract

- `original/`: unchanged upstream files, with source URL, author, version,
  license, original filename and SHA-256 recorded in a manifest.
- `solidcraft/`: editable `.sc` designs and repeatable JSON command scripts.
- `exports/`: generated STEP and STL/3MF files, tied to a source revision.
- `reference/`: measured dimensions and photographs, with units and provenance.

Create these directories when adding their first real assets. Preserve the
originals separately from modifications. An imported STL is a mesh reference,
not a recovered parametric feature history.

For every part, record a stable ID, quantity, source, revision, units, print
orientation/material/settings, fasteners and status: reference only, modeled,
fit checked, or physically verified. Keep measured dimensions separate from
assumed dimensions. Record the robot and date for physical checks.

## Reconstruction order

1. Recover the original Coglet files and establish attribution and reuse terms.
2. Inventory printed parts, servo models, horns, linkages and fasteners.
3. Establish assembly datums and measure mating interfaces and motion axes.
4. Recreate the original structure in SolidCraft, one named part at a time;
   compare dimensions and silhouettes with the originals.
5. Model the carrier and electronics envelopes, including underside solder,
   vertical amp/mic, Sense stack, USB plug and cable bends. Carrier dimensions
   start from `tools/carrier/carrier.py`; do not duplicate electrical layout logic.
6. Design the removable carrier mount and any required shell modifications.
   Determine dimensions from the actual enclosure; no mounting holes or cuts
   are specified yet. Check microphone direction, camera view, service access,
   insulation, strain relief and linkage motion clearance.
7. Export revisioned print files and installation drawings; dry-fit the parts
   before recording physical verification in `docs/QUALIFICATION.md`.

See [SolidCraft setup](../../docs/SOLIDCRAFT.md) and
[builder review](../../docs/BUILD-REVIEW.md).
