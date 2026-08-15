#!/bin/sh
set -eu

label="com.wt32.dashboard-gateway"
domain="gui/$(id -u)"

launchctl print "$domain/$label" | sed -n '1,35p'
curl --fail --silent --show-error --max-time 3 http://127.0.0.1:8080/healthz
printf '\n'
