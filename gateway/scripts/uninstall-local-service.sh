#!/bin/sh
set -eu

label="com.wt32.dashboard-gateway"
domain="gui/$(id -u)"
plist="$HOME/Library/LaunchAgents/$label.plist"

launchctl bootout "$domain/$label" >/dev/null 2>&1 || true
rm -f "$plist"
echo "Stopped and removed $label"
