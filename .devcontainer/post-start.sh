#!/usr/bin/env bash
# Every container start: (re)start Xvfb on :99 for the SDL host panel and
# headless screenshots. No-op when it's already running. -nolisten tcp keeps
# the display local to the container.

set -euo pipefail

if ! pgrep -x Xvfb >/dev/null 2>&1; then
	Xvfb :99 -screen 0 1280x800x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
fi
