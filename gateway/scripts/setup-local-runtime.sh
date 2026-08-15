#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
gateway_dir=$(dirname -- "$script_dir")

if [ -x /opt/homebrew/bin/python3 ]; then
    python=/opt/homebrew/bin/python3
else
    python=$(command -v python3)
fi

if ! "$python" -c 'import sys; raise SystemExit(sys.version_info < (3, 11))'; then
    echo "Python 3.11 or newer is required; found: $($python --version 2>&1)" >&2
    exit 1
fi

"$python" -m venv "$gateway_dir/.venv"
"$gateway_dir/.venv/bin/python" -m pip install --requirement "$gateway_dir/requirements.lock"
"$gateway_dir/.venv/bin/python" -c 'import fastapi, pydantic, uvicorn; print("Gateway runtime ready")'
