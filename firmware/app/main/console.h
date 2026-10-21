#pragma once

#include <string>

// The robot's command set, in serial-line form: !wifi, !server, !status, !mic,
// !speaker, !stop, !quiet, !reboot, !help, and !coglet for the controller.
// MCP tools and the serial console both run commands through Run, so there is
// one command set. The serial port exists for robots that aren't on a network
// yet.
namespace console {
void Start();
// Runs one command line and returns what it printed.
std::string Run(const std::string& line);
}
