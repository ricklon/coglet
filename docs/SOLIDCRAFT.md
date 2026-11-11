# SolidCraft for Coglet

SolidCraft is a host-side CAD tool. Its MCP server edits design files; it is
separate from the robot's `self.coglet.*` MCP tools and needs no firmware change.

Upstream: [tangency-labs/solidcraft](https://github.com/tangency-labs/solidcraft).
See its [scripts](https://github.com/tangency-labs/solidcraft/blob/main/docs/scripts.md)
and [MCP documentation](https://github.com/tangency-labs/solidcraft/blob/main/docs/mcp.md).
The local CLI inspected on 2026-11-10 is **0.30.1**. Upstream main may document
features newer than this installation; use the installed command registry.

## CLI

From the repository root in PowerShell:

```powershell
./tools/solidcraft.ps1
./tools/solidcraft.ps1 commands
./tools/solidcraft.ps1 run hardware/mechanical/solidcraft/part.json --out hardware/mechanical/exports/part.step --save hardware/mechanical/solidcraft/part.sc
```

The last command is a usage template: no `part.json` exists yet. Create output
directories before running it. The launcher finds `solidcraft-cli` on PATH or
in the standard Windows installation. `SOLIDCRAFT_CLI` can specify another
executable. On other platforms use `solidcraft-cli` directly with the same
arguments. Record the version used alongside generated assets.

## MCP

Start the project server with `pwsh -NoProfile -File tools/solidcraft.ps1 mcp`.
For MCP clients using `mcpServers`, adapt this example with the absolute checkout
path (and the absolute `pwsh` path if it is not on PATH):

```json
{
  "mcpServers": {
    "coglet-cad": {
      "command": "pwsh",
      "args": ["-NoProfile", "-File", "D:/OneDrive/Documents/GitHub/coglet/tools/solidcraft.ps1", "mcp"]
    }
  }
}
```

The launcher forces a separate headless design session and explicitly allows
`hardware/mechanical` for CAD reads and writes. SolidCraft also provides its
own default scratch/recovery access. It does not register a server in your
client automatically; configure the entry and reconnect that client to expose
the tools in a chat. Other client configuration formats must express the same
command and arguments.

Use `list_commands` and `describe_command` before modeling, `measure` and
`inspect_design` to check results, and `save`/`export` for editable and printable
artifacts. Use absolute paths in MCP calls. Preserve the original geometry;
record every mounting change and the dimensions that justify it.
