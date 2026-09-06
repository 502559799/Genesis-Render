#!/usr/bin/env bash
set -euo pipefail
case "$(uname -s)" in
  Darwin) exec "$(dirname "$0")/build_macos.sh" "$@" ;;
  *)      exec "$(dirname "$0")/build_linux.sh" "$@" ;;
esac
