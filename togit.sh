#!/usr/bin/env bash
# git add -A, commit (if there are changes), push to first remote.
# Usage: ./togit.sh ["commit message"]
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

if [ -n "$(git status --porcelain)" ]; then
  MSG="${*:-ParaEQ 301: snapshot}"
  git commit -m "$MSG"
  echo "togit.sh: committed — $MSG"
else
  echo "togit.sh: nothing new to stage/commit (tree already matched index)."
fi

REMOTE=$(git remote | head -n1 || true)
if [ -z "$REMOTE" ]; then
  echo "togit.sh: no git remote — cannot push. Add one, e.g.:" >&2
  echo "  git remote add origin https://github.com/you/paraEQ.git" >&2
  exit 1
fi

BR=$(git branch --show-current)
if [ -z "$BR" ]; then
  echo "togit.sh: not on a named branch (detached HEAD) — push manually." >&2
  exit 1
fi

git push -u "$REMOTE" "$BR"
echo "togit.sh: pushed branch \"$BR\" to remote \"$REMOTE\""
