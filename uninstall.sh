#!/bin/bash

# Uninstallation script for smart-cmd

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Smart Command Completion Uninstallation"
echo "====================================="

# Stop running daemon first
echo "Stopping any running daemon..."
if "$HOME/.local/bin/smart-cmd-daemon" --status >/dev/null 2>&1; then
    "$HOME/.local/bin/smart-cmd-daemon" --stop
    echo "✓ Daemon stopped"
else
    echo "✓ No running daemon found"
fi

# Remove systemd user service if it exists
SERVICE_FILE="$HOME/.config/systemd/user/smart-cmd-daemon.service"
if [[ -f "$SERVICE_FILE" ]]; then
    echo "Removing systemd user service..."
    systemctl --user stop smart-cmd-daemon.service 2>/dev/null || true
    systemctl --user disable smart-cmd-daemon.service 2>/dev/null || true
    systemctl --user daemon-reload
    rm -f "$SERVICE_FILE"
    echo "✓ Systemd service removed"
fi

# Remove binaries
echo "Removing installed binaries..."
rm -f "$HOME/.local/bin/smart-cmd-completion"
rm -f "$HOME/.local/bin/smart-cmd-daemon"
rm -f "$HOME/.local/bin/smart-cmd"
rm -f "$HOME/.local/bin/smart-cmd.bash"
echo "✓ Binaries removed from ~/.local/bin/"

# Remove bash integration from ~/.bashrc
BASHRC="$HOME/.bashrc"
SMART_CMD_LINE="source $HOME/.local/bin/smart-cmd.bash"

if grep -q "smart-cmd.bash" "$BASHRC"; then
    echo "Removing smart-cmd integration from $BASHRC..."

    # Create backup
    cp "$BASHRC" "$BASHRC.smart-cmd-backup"

    # Remove smart-cmd lines
    sed -i '/# Smart Command Completion/,+1d' "$BASHRC"

    echo "✓ Bash integration removed"
    echo "  Backup saved as: $BASHRC.smart-cmd-backup"
else
    echo "✓ No bash integration found in $BASHRC"
fi

# Clean up daemon files in /tmp
echo "Cleaning up daemon temporary files..."
TMP_DIR="${TMPDIR:-/tmp}"
rm -f "$TMP_DIR"/smart-cmd.lock.*
rm -f "$TMP_DIR"/smart-cmd.socket.*
rm -f "$TMP_DIR"/smart-cmd.log.*
rm -f "$TMP_DIR"/smart-cmd.history.*
echo "✓ Temporary files cleaned up"

# Ask about config file
CONFIG_DIR="$HOME/.config/smart-cmd"
CONFIG_FILE="$CONFIG_DIR/config.json"

if [[ -f "$CONFIG_FILE" ]]; then
    echo ""
    read -p "❓ Remove configuration directory $CONFIG_DIR? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "$CONFIG_DIR"
        echo "✓ Configuration directory removed"
    else
        echo "✓ Configuration directory preserved"
        echo "  You can manually remove it later: rm -rf $CONFIG_DIR"
    fi
fi

# Check for any remaining smart-cmd processes
echo ""
echo "Checking for remaining smart-cmd processes..."
if pgrep -f "smart-cmd" >/dev/null 2>&1; then
    echo "⚠️  Found remaining smart-cmd processes:"
    pgrep -af "smart-cmd" || true
    echo ""
    echo "Stopping all smart-cmd processes..."
    pkill -f "smart-cmd" || true
    echo "✓ All smart-cmd processes stopped"
else
    echo "✓ No remaining smart-cmd processes found"
fi

echo ""
echo "Uninstallation completed!"
echo ""
echo "To complete the cleanup:"
echo "1. Restart your terminal or run: source ~/.bashrc"
echo "2. The bash integration has been removed from your shell"
echo ""
echo "Your config files were preserved. If you want to remove them:"
echo "  rm -rf ~/.config/smart-cmd"
echo ""
echo "Thank you for using smart-cmd!"