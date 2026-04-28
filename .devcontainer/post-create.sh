#!/usr/bin/env bash
set -euo pipefail

if ! command -v node >/dev/null 2>&1; then
  echo "Node.js not found; Claude CLI install skipped."
  exit 0
fi

if ! command -v claude >/dev/null 2>&1; then
  # Official CLI package. If npm install fails, we keep container usable.
  npm install -g @anthropic-ai/claude-code || true
fi
