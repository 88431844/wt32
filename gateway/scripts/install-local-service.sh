#!/bin/sh
set -eu

label="com.wt32.dashboard-gateway"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
gateway_dir=$(dirname -- "$script_dir")
template="$gateway_dir/launchd/$label.plist.in"
launch_agents_dir="$HOME/Library/LaunchAgents"
plist="$launch_agents_dir/$label.plist"
domain="gui/$(id -u)"

if [ ! -x "$gateway_dir/.venv/bin/python" ]; then
    echo "Missing $gateway_dir/.venv; run scripts/setup-local-runtime.sh first." >&2
    exit 1
fi

mkdir -p "$launch_agents_dir" "$gateway_dir/logs"
escaped_dir=$(printf '%s' "$gateway_dir" | sed 's/[\/&]/\\&/g')
sed "s/__GATEWAY_DIR__/$escaped_dir/g" "$template" > "$plist"
plutil -lint "$plist" >/dev/null

launchctl bootout "$domain/$label" >/dev/null 2>&1 || true
sleep 0.5

bootstrap_attempt=0
while ! launchctl bootstrap "$domain" "$plist"; do
    bootstrap_attempt=$((bootstrap_attempt + 1))
    if [ "$bootstrap_attempt" -ge 10 ]; then
        echo "Unable to register $label after 10 attempts." >&2
        exit 1
    fi
    sleep 0.5
done
launchctl enable "$domain/$label"
launchctl kickstart -k "$domain/$label"

attempt=0
while [ "$attempt" -lt 40 ]; do
    if curl --fail --silent --max-time 1 http://127.0.0.1:8080/healthz >/dev/null 2>&1; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.25
done

if [ "$attempt" -ge 40 ]; then
    echo "Gateway did not become healthy within 10 seconds." >&2
    tail -40 "$gateway_dir/logs/gateway.stderr.log" >&2 || true
    exit 1
fi

echo "Started $label"
echo "Health: http://127.0.0.1:8080/healthz"
echo "Docs:   http://127.0.0.1:8080/docs"
