#!/usr/bin/env bash
set -euo pipefail

# Add the container user to the dialout group for serial port access.
sudo usermod -aG dialout "${REMOTE_USER:-vscode}"

# Add PlatformIO to PATH in ~/.zshrc
echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.zshrc
