#!/usr/bin/env bash
# Stage everything (honours .gitignore), commit. Usage: ./togit.sh ["commit message"]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if ! command -v git >/dev/null 2>&1; then
  echo "togit.sh: git not found." >&2
  exit 1
fi

if [ ! -d .git ]; then
  git init
  echo "togit.sh: initialized git repository in $ROOT"
fi

git add -A

if [ -z "$(git status --porcelain)" ]; then
  echo "togit.sh: nothing to commit (clean tree)."
  exit 0
fi

MSG="${*:-ParaEQ 301: snapshot}"
git commit -m "$MSG"
echo "togit.sh: committed — $MSG"
