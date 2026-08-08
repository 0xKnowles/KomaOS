#!/usr/bin/env bash
#
# Print only the errors from a repository's most recent failed CI run.
#
# GitHub's job logs are mostly checkout and credential-cleanup noise -- the
# compile error that actually matters arrives wrapped in a hundred lines of
# 'git config --unset includeif.gitdir:...'. Fetching them through an agent's
# tool call pulls all of that into its context; piping them through here keeps
# everything but the matching lines out.
#
# Usage:
#   scripts/ci-errors.sh                       # this repo, latest failed run
#   scripts/ci-errors.sh 0xKnowles/FlipNzb     # another repo
#   scripts/ci-errors.sh 0xKnowles/KomaOS 123  # a specific run id
#
# Needs GITHUB_TOKEN (or gh's token) for a private repo.

set -euo pipefail

REPO="${1:-}"
RUN_ID="${2:-}"

if [[ -z "$REPO" ]]; then
  origin=$(git config --get remote.origin.url || true)
  REPO=$(sed -E 's#.*github\.com[:/]([^/]+/[^/.]+)(\.git)?$#\1#' <<<"$origin")
  [[ -n "$REPO" && "$REPO" != "$origin" ]] || { echo "Could not infer repo; pass owner/name" >&2; exit 2; }
fi

TOKEN="${GITHUB_TOKEN:-${GH_TOKEN:-}}"
auth=()
[[ -n "$TOKEN" ]] && auth=(-H "Authorization: Bearer $TOKEN")

api() { curl -sS --fail-with-body "${auth[@]}" -H 'Accept: application/vnd.github+json' "$@"; }

if [[ -z "$RUN_ID" ]]; then
  branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '')
  q="https://api.github.com/repos/$REPO/actions/runs?per_page=20"
  [[ -n "$branch" ]] && q="$q&branch=$branch"
  # Newest run that actually failed. Parsed here rather than returned whole:
  # the runs listing carries a full repository object per run and runs to a
  # quarter of a megabyte for 20 of them.
  read -r RUN_ID RUN_SHA RUN_TITLE < <(api "$q" | python3 -c '
import sys, json
runs = json.load(sys.stdin).get("workflow_runs", [])
for r in runs:
    if r.get("conclusion") == "failure":
        print(r["id"], r["head_sha"][:8], r["display_title"][:60]); break
else:
    print("", "", "")
')
  [[ -n "$RUN_ID" ]] || { echo "No failed run found on ${branch:-this repo}."; exit 0; }
  echo "run $RUN_ID  $RUN_SHA  $RUN_TITLE"
fi

echo "--- failing jobs ---"
api "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/jobs?per_page=50" | python3 -c '
import sys, json
for j in json.load(sys.stdin).get("jobs", []):
    if j.get("conclusion") == "failure":
        print("{}\t{}".format(j["id"], j["name"]))
' | while IFS=$'\t' read -r job_id job_name; do
  echo
  echo "=== $job_name ==="
  # Logs are fetched and filtered in the pipe; the full text never lands
  # anywhere a reader has to page through.
  raw=$(api -L "https://api.github.com/repos/$REPO/actions/jobs/$job_id/logs" 2>/dev/null) || {
    # Distinguished from "found nothing": the logs endpoint always needs a
    # token, even on a public repo, and silently reporting no errors when the
    # fetch never happened is worse than saying so.
    echo "  could not fetch logs -- set GITHUB_TOKEN (needed even for public repos)"
    continue
  }
  hits=$(printf '%s\n' "$raw" \
    | sed -E 's/^[0-9T:.-]+Z //' \
    | grep -E -i 'error|FAILED|\*\*\*|Assertion|undefined reference|No such file' \
    | grep -v -E 'error\.log|--unset|includeif|extraheader|npm notice' \
    | head -40 || true)
  if [[ -n "$hits" ]]; then printf '%s\n' "$hits"; else echo "  (job failed with no matching error line)"; fi
done
