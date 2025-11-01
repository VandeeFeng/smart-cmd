#!/bin/bash

# Installation script for smart-cmd bash integration

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASH_SCRIPT="$SCRIPT_DIR/smart-cmd.bash"

echo "Smart Command Completion Installation"
echo "===================================="

# Check if nob build system exists and build it if needed
if [[ ! -f "$SCRIPT_DIR/nob" ]]; then
    echo "Building nob build system..."
    cd "$SCRIPT_DIR"
    cc -o nob nob.c
else
    echo "Nob build system found"
fi

# Always run nob build (it will automatically check if rebuild is needed)
echo "Building smart-cmd with nob..."
cd "$SCRIPT_DIR"
./nob

# Create ~/.local/bin if it doesn't exist
mkdir -p "$HOME/.local/bin"

# Stop daemon if it's running (after build, before install)
daemon_was_running=0
if pgrep -f "smart-cmd-daemon" > /dev/null; then
    echo "Stopping daemon for update..."
    daemon_was_running=1

    # Try to stop gracefully first
    if [[ -x "$HOME/.local/bin/smart-cmd-daemon" ]]; then
        "$HOME/.local/bin/smart-cmd-daemon" --stop >/dev/null 2>&1 || true
        sleep 1
    fi

    # Force kill any remaining processes
    pkill -f "smart-cmd-daemon" >/dev/null 2>&1 || true
    sleep 1

    # Check if daemon was successfully stopped
    if pgrep -f "smart-cmd-daemon" > /dev/null; then
        echo "⚠️  Warning: Failed to stop some daemon processes"
    else
        echo "✓ Daemon stopped successfully"
    fi
fi

# Copy all binaries and bash script
echo "Installing smart-cmd components to $HOME/.local/bin/..."
cp "$SCRIPT_DIR/smart-cmd-completion" "$HOME/.local/bin/"
cp "$SCRIPT_DIR/smart-cmd-daemon" "$HOME/.local/bin/"
cp "$SCRIPT_DIR/smart-cmd" "$HOME/.local/bin/"
cp "$SCRIPT_DIR/smart-cmd.bash" "$HOME/.local/bin/"

# Set executable permissions
echo ""
echo "Setting executable permissions..."
echo "⚠️  These files need execute permissions to run:"
echo "   • smart-cmd-completion: Fallback completion backend"
echo "   • smart-cmd-daemon: Advanced daemon with PTY support"
echo "   • smart-cmd: Main utility program"
echo "   • smart-cmd.bash: Bash script that integrates with your shell"
echo ""

read -p "❓ Set executable permissions? (Y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Nn]$ ]]; then
    echo "⚠️  Skipping permissions. You'll need to run manually:"
    echo "   chmod +x ~/.local/bin/smart-cmd-completion"
    echo "   chmod +x ~/.local/bin/smart-cmd.bash"
else
    echo "✅ Setting executable permissions..."
    chmod +x "$HOME/.local/bin/smart-cmd-completion"
    chmod +x "$HOME/.local/bin/smart-cmd-daemon"
    chmod +x "$HOME/.local/bin/smart-cmd"
    chmod +x "$HOME/.local/bin/smart-cmd.bash"
    echo "   ✓ All binaries are now executable"
fi

# Restart daemon if it was running before
if [[ $daemon_was_running -eq 1 ]]; then
    echo "Restarting smart-cmd daemon..."
    "$HOME/.local/bin/smart-cmd-daemon" >/dev/null 2>&1 &
    sleep 1 # Give it a moment to start
    if pgrep -f "smart-cmd-daemon" > /dev/null; then
        echo "✓ Daemon restarted successfully"
    else
        echo "⚠️  Failed to restart daemon. Please start it manually with 'smart-cmd-start'."
    fi
fi

# Create config directory
mkdir -p "$HOME/.config/smart-cmd"

# Copy config if it doesn't exist (prefer example_config.json over config.json)
if [[ -f "$HOME/.config/smart-cmd/config.json" ]]; then
    echo "✓ Config file already exists at $HOME/.config/smart-cmd/config.json - skipping copy"
else
    echo "Installing config to $HOME/.config/smart-cmd/..."
    if [[ -f "$SCRIPT_DIR/example_config.json" ]]; then
        cp "$SCRIPT_DIR/example_config.json" "$HOME/.config/smart-cmd/config.json"
    elif [[ -f "$SCRIPT_DIR/config.json" ]]; then
        cp "$SCRIPT_DIR/config.json" "$HOME/.config/smart-cmd/config.json"
    else
        echo "Warning: No config file found, you'll need to create $HOME/.config/smart-cmd/config.json manually"
    fi
fi

# Update bash script with correct paths
echo "Updating bash script with correct paths..."
sed -i "s|_SMART_CMD_BIN=\"\${SMART_CMD_BIN:-\$HOME/.local/bin/smart-cmd-completion}\"|_SMART_CMD_BIN=\"\${SMART_CMD_BIN:-$HOME/.local/bin/smart-cmd-completion}\"|g" "$HOME/.local/bin/smart-cmd.bash"
sed -i "s|_SMART_CMD_DAEMON_BIN=\"\${SMART_CMD_DAEMON_BIN:-\$HOME/.local/bin/smart-cmd-daemon}\"|_SMART_CMD_DAEMON_BIN=\"\${SMART_CMD_DAEMON_BIN:-$HOME/.local/bin/smart-cmd-daemon}\"|g" "$HOME/.local/bin/smart-cmd.bash"

# Check if ~/.bashrc already sources the script
BASHRC="$HOME/.bashrc"
SMART_CMD_LINE="source $HOME/.local/bin/smart-cmd.bash"

if ! grep -q "smart-cmd.bash" "$BASHRC"; then
    echo ""
    echo "Adding smart-cmd to $BASHRC..."
    echo "" >> "$BASHRC"
    echo "# Smart Command Completion" >> "$BASHRC"
    echo "$SMART_CMD_LINE" >> "$BASHRC"
else
    echo "smart-cmd already configured in $BASHRC"
fi

# Optional: Install systemd user service
read -p "❓ Install systemd user service for auto-starting daemon? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    SERVICE_DIR="$HOME/.config/systemd/user"
    mkdir -p "$SERVICE_DIR"

    cat > "$SERVICE_DIR/smart-cmd-daemon.service" << EOF
[Unit]
Description=Smart Command Daemon
After=graphical-session.target

[Service]
Type=forking
ExecStart=$HOME/.local/bin/smart-cmd-daemon
ExecStop=$HOME/.local/bin/smart-cmd-daemon --stop
Restart=on-failure
RestartSec=5
Environment=HOME=$HOME
Environment=USER=$USER

[Install]
WantedBy=default.target
EOF

    systemctl --user daemon-reload
    systemctl --user enable smart-cmd-daemon.service

    echo "✅ Systemd service installed and enabled"
    echo "   Daemon will auto-start on login"
fi

echo ""
echo "Installation completed!"
echo ""
echo "Components installed:"
echo "  ✓ smart-cmd: Main utility program"
echo "  ✓ smart-cmd-completion: Fallback completion backend"
echo "  ✓ smart-cmd-daemon: Advanced daemon with PTY support"
echo "  ✓ smart-cmd.bash: Bash integration script"
echo ""
echo "To use smart-cmd:"
echo "1. Restart your terminal or run: source ~/.bashrc"
echo "2. Type a partial command and press Ctrl+O"
echo "3. Use 'smart-cmd-toggle' to enable/disable"
echo "4. Use 'smart-cmd-start'/'smart-cmd-stop'/'smart-cmd-status' to manage daemon"
echo ""
echo "Example: type 'git s' and press Ctrl+O"
echo ""
echo "🔧 Daemon features:"
echo "  - Persistent PTY sessions for rich context"
echo "  - Unix Domain Socket for fast communication"
echo "  - Secure process isolation and session management"
echo "  - Automatic fallback to completion backend"