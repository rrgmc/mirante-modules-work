// The status of a Claude agents list: runs `claude agents --json` once per
// account, then finishes the job with agentsStatus(). See decision 0143 in
// mirante-core.
#pragma once

#include <QString>

namespace mirante {
class StatusJob;
}

namespace mirante::modules::claude {

// For ItemType::fetchStatus(). program is the Claude Code program; empty
// finds it with findClaudeProgram(). Tests pass another program.
void fetchAgents(StatusJob &job, const QString &program = QString());

} // namespace mirante::modules::claude
