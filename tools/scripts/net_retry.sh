#!/usr/bin/env bash
# A network step of the build (clone, download) with a retry on a transient network failure: net_retry.sh <command>...
#
# A transient failure is a pattern from TRANSIENT in the last lines of the error output of the failed attempt. It is
# retried: up to NET_RETRY_ATTEMPTS attempts (3), the pause before the n-th retry is n*NET_RETRY_DELAY seconds (20).
# Other errors (no branch, no such object, no access) and the final failure are returned with the original exit code.
# Builds and tests do not go through the retry: only network commands are wrapped.
set -u

# Patterns of a transient network failure (ERE, case-insensitive). Whatever is not here is considered a deterministic
# error and is not retried.
TRANSIENT=(
  # git and curl: disconnects, timeouts, low-speed abort ("Operation too slow"), TLS.
  'RPC failed' 'early EOF' 'remote end hung up' 'unexpected disconnect' 'transfer closed' 'Empty reply from server'
  'HTTP/2 stream' 'Operation (timed out|too slow)' 'gnutls' 'SSL_(read|write|connect|ERROR)' 'SSL connection'
  # DNS and connection.
  'Could not resolve host' 'Temporary failure in name resolution' 'Failed to connect' 'Network is unreachable'
  'No route to host' 'Connection (reset|refused|timed out|aborted|broken)' 'timed out'
  # HTTP 5xx.
  'returned error: 5[0-9][0-9]' 'HTTP error 5[0-9][0-9]' '5[0-9][0-9] Server Error'
  '50[0-4] (Internal Server Error|Bad Gateway|Service Unavailable|Gateway Time-?out)'
  # GitLab servers: an internal failure during a clone ("remote: Internal API unreachable").
  'Internal API unreachable'
  # git: a truncated protocol or pack stream (on an HTTP 500 of the server).
  'expected flush after ref listing' "expected 'packfile'" 'bytes of body are still expected' 'invalid index-pack output'
)
TRANSIENT_RE="$(IFS='|'; echo "${TRANSIENT[*]}")"
readonly TRANSIENT_RE
# The pattern is searched only in the tail of the output: a failure line in the middle (for example, a retry that the
# command recovered from) does not make the error the command ended with transient.
readonly TAIL_LINES=20

attempts="${NET_RETRY_ATTEMPTS:-3}"
delay="${NET_RETRY_DELAY:-20}"
# Only the command name goes to the log: the arguments may carry tokens in URLs.
what="$1 ${2:-}"
errors="$(mktemp)" || exit 1
trap 'rm -f "$errors"' EXIT
for ((attempt = 1; ; attempt++)); do
  # stderr goes both to the log and to a file for analysis; stdout is left untouched.
  { "$@" 2>&1 1>&3 3>&- | tee "$errors" >&2; } 3>&1
  status=${PIPESTATUS[0]}
  ((status == 0)) && exit 0
  if ! tail -n "$TAIL_LINES" "$errors" | grep -Eqi -- "$TRANSIENT_RE"; then
    echo "net-retry: '$what' — exit code $status, not a network failure: no retry" >&2
    exit "$status"
  fi
  if ((attempt >= attempts)); then
    echo "net-retry: '$what' — the network failure persisted for $attempts attempts, exit code $status" >&2
    exit "$status"
  fi
  echo "net-retry: '$what' — network failure (exit code $status), attempt $((attempt + 1))/$attempts in $((attempt * delay)) s" >&2
  sleep "$((attempt * delay))"
done
