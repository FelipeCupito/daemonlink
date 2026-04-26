#!/usr/bin/env bash
# ============================================================================
#  scripts/preview.sh
#  Local preview of the DaemonLink PWA — no deploy, no CI wait.
#
#  Why a script instead of "just run python3 -m http.server"?
#    1. Always serves from web/, regardless of cwd.
#    2. Prints the LAN IP so you can open it from a phone on the same Wi-Fi
#       (Chrome desktop accepts http://localhost; Android Chrome needs http
#       *or* https — LAN IP works for layout review, but Web Serial is
#       gated to localhost / https only, so for full TX/RX testing on the
#       phone use a Firebase preview channel instead).
#    3. One Ctrl-C, one stop. No background processes left behind.
#
#  Usage:
#    ./scripts/preview.sh            # serves on :8000
#    ./scripts/preview.sh 9000       # custom port
# ============================================================================
set -euo pipefail

PORT="${1:-8000}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB="$ROOT/web"

if [[ ! -f "$WEB/index.html" ]]; then
  echo "preview: $WEB/index.html not found — are you in the DaemonLink repo?" >&2
  exit 1
fi

# Best-effort LAN IP (works on macOS + most Linuxes; falls back gracefully).
LAN_IP="$(ipconfig getifaddr en0 2>/dev/null \
       || ipconfig getifaddr en1 2>/dev/null \
       || hostname -I 2>/dev/null | awk '{print $1}' \
       || echo "")"

cat <<EOF

  ╔══════════════════════════════════════════════════════════════════╗
  ║  DaemonLink PWA — local preview                                  ║
  ╠══════════════════════════════════════════════════════════════════╣
  ║  Desktop  :  http://localhost:$PORT/                              ║
EOF
if [[ -n "$LAN_IP" ]]; then
  printf "  ║  Phone    :  http://%s:%s/   (layout only, no Web Serial)\n" "$LAN_IP" "$PORT"
fi
cat <<EOF
  ║                                                                  ║
  ║  Stop with Ctrl-C.                                               ║
  ╚══════════════════════════════════════════════════════════════════╝

EOF

cd "$WEB"
exec python3 -m http.server "$PORT"
