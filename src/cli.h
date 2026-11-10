#pragma once

#include <QStringList>

class AddonManager;

// Commands that need no display, for scripts and agents. Each one prints to
// stdout (JSON with --json), errors to stderr, and exits 0 or 1.
namespace Cli {
bool isCommand(const QString &word);
int run(AddonManager &manager, QStringList args);
}
