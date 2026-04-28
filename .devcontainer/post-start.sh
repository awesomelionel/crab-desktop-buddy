#!/usr/bin/env bash
set -euo pipefail

SHELL_RC="${HOME}/.bashrc"

if [ -f "${HOME}/.zshrc" ]; then
  SHELL_RC="${HOME}/.zshrc"
fi

if ! grep -q "^alias claude-skip=" "${SHELL_RC}" 2>/dev/null; then
  cat <<'EOF' >> "${SHELL_RC}"

# Claude helper for local container sessions.
alias claude-skip='claude --dangerously-skip-permissions'
EOF
fi
