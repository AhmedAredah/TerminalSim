#pragma once

#include <QtGlobal>

namespace TerminalSim {

class TerminalSimLogger;

// Installs a custom Qt message handler that:
//   1. Delegates to the previous handler for stderr output.
//   2. For all messages from terminalsim.* categories,
//      forwards to the provided TerminalSimLogger for file output.
//
// Pass nullptr for fileLogger to get stderr-only output (e.g. in tests).
void installTerminalSimLogHandler(
    TerminalSimLogger *fileLogger = nullptr);

} // namespace TerminalSim
