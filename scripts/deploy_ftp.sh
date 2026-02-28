#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VPK_PATH="$ROOT_DIR/build/vita_wifi_scope.vpk"
FTP_HOST="${FTP_HOST:-10.0.0.28}"
FTP_PORT="${FTP_PORT:-1337}"
FTP_TARGET_DIR="${FTP_TARGET_DIR:-ux0:/downloads}"
FTP_CONNECT_TIMEOUT="${FTP_CONNECT_TIMEOUT:-3}"
FTP_MAX_TIME="${FTP_MAX_TIME:-30}"
FTP_RETRY="${FTP_RETRY:-4}"
FTP_RETRY_DELAY="${FTP_RETRY_DELAY:-1}"

DIAG=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --diag)
      DIAG=1
      shift
      ;;
    *)
      VPK_PATH="$1"
      shift
      ;;
  esac
done

if [ ! -f "$VPK_PATH" ]; then
  echo "VPK not found: $VPK_PATH" >&2
  exit 1
fi

VPK_NAME="$(basename "$VPK_PATH")"
FTP_URL="ftp://${FTP_HOST}:${FTP_PORT}/${FTP_TARGET_DIR}/${VPK_NAME}"
FTP_BASE_URL="ftp://${FTP_HOST}:${FTP_PORT}/"

if [ "$DIAG" -eq 1 ]; then
  echo "Diagnostic: probing FTP endpoint ${FTP_BASE_URL}"
  curl -v --connect-timeout "$FTP_CONNECT_TIMEOUT" --max-time "$FTP_MAX_TIME" "$FTP_BASE_URL" || true
fi

echo "Uploading $VPK_NAME to ${FTP_HOST}:${FTP_PORT}/${FTP_TARGET_DIR}"
curl --fail --silent --show-error \
  --connect-timeout "$FTP_CONNECT_TIMEOUT" \
  --max-time "$FTP_MAX_TIME" \
  --retry "$FTP_RETRY" \
  --retry-delay "$FTP_RETRY_DELAY" \
  --ftp-create-dirs \
  -T "$VPK_PATH" \
  "$FTP_URL"
echo "Upload complete: $FTP_URL"
