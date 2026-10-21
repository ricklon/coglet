# Roadmap

## Done

- Coglet C3 and S3 profiles in the xiaozhi fork: voice, OTA, agent-hub check-in,
  `self.coglet.*` MCP tools (now the full command set; see OPERATION.md).
- Nine-role controller with released boot, builder mode, endpoint hunting
  (`identify`, `explore`, `nudge`, `mark`), NVS calibration and JSON export.
- `!coglet scan` bus diagnostics, voice quiet mode. (The builder web page was
  removed on 2026-10-20: MCP is the one interface.)
- Host tests against a fake ESP-IDF.
- Split into this repo, with its own Web Programmer and release workflow (2026-10-13).

## Next

- [ ] Write the mechanical steps in [ASSEMBLY.md](ASSEMBLY.md): printed parts,
      servo mounting, horns, linkages, clearances.
- [ ] Redraw `hardware/esp32c3-voice-agent.fzz` to the standard pad map.
- [ ] Rewire the bench C3 to the standard pad map: swap D2/D3 and D4/D5
      (see [WIRING.md](../hardware/WIRING.md#pad-map-every-board)).
- [ ] Flash `firmware/app` onto the bench C3 (servo rail off) and check:
      check-in, hello, a full voice turn, MCP discovery and a tool call, the
      boot button interrupting a reply, `!quiet`, builder-mode quiet, and that
      Wi-Fi, hub URL and calibration survived the move off the fork.
- [ ] Port the S3 (camera, its pins) to `firmware/app`, then drop the
      submodule. Until then, builder mode on the S3 closes the MCP session.
- [ ] Ask agent-hub to let its device proxy expose the new tools (it defaults
      to tools that can't start motion; `--all-tools` works today).
- [ ] CI: the firmware job still checks out the submodule for every board;
      only the S3 needs it.

- [ ] Push the fork's `feat/coglet-split` branch and pin the submodule to it.
- [ ] Create `ricklon/coglet` on GitHub, enable Pages (source: GitHub Actions)
      and tag `v0.1.0`.
- [ ] Merge agent-hub's `feat/device-mcp-proxy` so Claude Code can drive Coglet
      from a `main` checkout.
- [ ] Finish fitting and calibrating the C3 mechanism, base upward. Record each
      stage in [QUALIFICATION.md](QUALIFICATION.md).
- [ ] Record the servo model and supply rating, and measure the real pulse range.
- [ ] Work out whether `/OE` is wired on the breakout. If not, wire D10 and add
      a pull-up.

## Later

- Voice-guided calibration, where the robot talks you through it.
- Neck motion that follows gaze beyond the eyes' travel.
- Jaw motion tied to speech. Ear choreography.
- A listen-mode indicator LED on the C3 (ricklon/xiaozhi-esp32#8).
- Confirm the S3's camera module and enable it.
- A persona prompt and dashboard buttons for Coglet in agent-hub.
