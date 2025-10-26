#!/bin/bash

# Installation script for smart-cmd bash integration

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASH_SCRIPT="$SCRIPT_DIR/smart-cmd.bash"

echo "Smart Command Completion Installation"
echo "===================================="

# Check if binaries exist in project root
if [[ ! -f "$SCRIPT_DIR/smart-cmd-completion" ]]; then
    echo "Building smart-cmd..."
    cd "$SCRIPT_DIR"
    make clean && make
fi

# Create ~/.local/bin if it doesn't exist
mkdir -p "$HOME/.local/bin"

# Copy completion binary and bash script
echo "Installing smart-cmd-completion to $HOME/.local/bin..."
cp "$SCRIPT_DIR/smart-cmd-completion" "$HOME/.local/bin/"
echo "Installing smart-cmd.bash to $HOME/.local/bin..."
cp "$SCRIPT_DIR/smart-cmd.bash" "$HOME/.local/bin/"

# Set executable permissions
echo ""
echo "Setting executable permissions..."
echo "⚠️  These files need execute permissions to run:"
echo "   • smart-cmd-completion: C program that provides AI suggestions"
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
    chmod +x "$HOME/.local/bin/smart-cmd.bash"
    echo "   ✓ smart-cmd-completion is now executable"
    echo "   ✓ smart-cmd.bash is now executable"
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
sed -i "s|$SCRIPT_DIR/smart-cmd-completion|$HOME/.local/bin/smart-cmd-completion|g" "$SCRIPT_DIR/smart-cmd.bash"
cp "$SCRIPT_DIR/smart-cmd.bash" "$HOME/.local/bin/"

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

echo ""
echo "Installation completed!"
echo ""
echo "To use smart-cmd:"
echo "1. Restart your terminal or run: source ~/.bashrc"
echo "2. Type a partial command and press Ctrl+O"
echo "3. Use 'smart-cmd-toggle' to enable/disable"
echo ""
echo "Example: type 'git s' and press Ctrl+O"