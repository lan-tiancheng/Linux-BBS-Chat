#!/bin/sh
set -eu

make all
exec python3 web/server.py
