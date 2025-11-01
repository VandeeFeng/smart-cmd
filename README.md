# Smart Command Completion

Inspired by [yetone/smart-suggestion](https://github.com/yetone/smart-suggestion)

I primarily use bash, but smart-suggestion only supports zsh. So I implemented this in C for bash.

It's a lightweight version with fundamental bash completion.

## Installation

### Quick Install

```bash
# Clone the repository
git clone https://github.com/vandeefeng/smart-cmd.git
cd smart-cmd

# Run the installation script
./install.sh

# Reload your shell configuration
source ~/.bashrc

# Verify installation (optional)
smart-cmd help or --help
smart-cmd-mode
```

**install.sh**: Automated installation script that uses [nob.h](https://github.com/tsoding/nob.h) build system to compile and install all components
**uninstall.sh**: Clean removal script that safely uninstalls all components

**Important**: Make sure `~/.local/bin` is in your PATH. If commands are not found, add this to your `~/.bashrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
source ~/.bashrc
```

### Manual Installation

If you prefer manual installation:

```bash
# Initialize git submodules
git submodule update --init --recursive

# Build the project (uses nob.h build system)
./nob
# Or use make if you prefer: make clean && make

# Install binaries
cp smart-cmd ~/.local/bin/
cp smart-cmd-completion ~/.local/bin/
cp smart-cmd-daemon ~/.local/bin/
cp smart-cmd.bash ~/.local/bin/

# Create config directory and install configuration
mkdir -p ~/.config/smart-cmd/
cp example_config.json ~/.config/smart-cmd/config.json

# Set executable permissions
chmod +x ~/.local/bin/smart-cmd
chmod +x ~/.local/bin/smart-cmd-completion
chmod +x ~/.local/bin/smart-cmd-daemon
chmod +x ~/.local/bin/smart-cmd.bash

# Add to bashrc (if not automatically added)
echo 'source ~/.local/bin/smart-cmd.bash' >> ~/.bashrc
source ~/.bashrc
```

## Usage

### Basic Controls

1. **Trigger AI Completion**: Press `Ctrl+O` to send your current command to the LLM API
2. **Accept Suggestion**: Press `→` (right arrow) to confirm and fill the LLM's completion suggestion
3. **Cancel**: Press `Esc` or continue typing normally to ignore suggestions

### Working Modes

Smart-cmd supports two modes controlled by `enable_proxy_mode` in the configuration:

#### Basic Mode (`enable_proxy_mode: false`)
- **Direct AI completion** without persistent context
- **Fast response time** with minimal overhead
- **Environment context** (cwd, git status, user info)
- **No command history** - each request is independent

#### Daemon Mode (`enable_proxy_mode: true`)
- **Command history** - remembers last 50 commands (1 hour window)
- **Context-aware suggestions** - AI learns from your recent commands
- **Session persistence** - history survives shell restarts
- **Secure isolation** - daemon runs in isolated environment for privacy

**Security Features:**
- Command history stored in temporary files (`/tmp/smart-cmd.history.{session}`)
- Commands older than 1 hour are automatically deleted
- Only last 3 commands are sent to AI for context
- Completely isolated from your bash history

**Communication:** Daemon communicates through Unix Domain Sockets (`/tmp/smart-cmd.socket.{session_id}`) for secure IPC.

### Daemon Management

When using daemon mode, you have additional commands (available globally):

```bash
# Check daemon status and current mode
smart-cmd-status

# View detailed configuration and mode information
smart-cmd-mode

# Manually start daemon (if not auto-starting)
smart-cmd-start

# Stop daemon
smart-cmd-stop

# Toggle smart completion on/off
smart-cmd-toggle

# View version information
smart-cmd --version

# Show help and all available commands
smart-cmd --help

# Test basic functionality
smart-cmd --test

# Show current configuration
smart-cmd --config
```

### Configuration Mode Switching

To switch between modes:

```bash
# Edit configuration
vim ~/.config/smart-cmd/config.json

# Change enable_proxy_mode setting
{
  "llm": { ... },
  "trigger_key": "ctrl+o",
  "enable_proxy_mode": true  // true for daemon, false for basic
}

# Reload shell configuration
source ~/.bashrc
```

### Custom Keyboard Shortcuts

You can customize the keyboard shortcuts in the configuration file:

```json
{
  "trigger_key": "ctrl+o",        // Key to trigger AI completion
}
```

## Configuration

### API Key Setup

Set your API keys as environment variables:

```bash
# For OpenAI
export OPENAI_API_KEY="your-openai-api-key"

# For Google Gemini
export GEMINI_API_KEY="your-gemini-api-key"

# For OpenRouter
export OPENROUTER_API_KEY="your-openrouter-api-key"
```

### LLM Provider Configuration

Edit the configuration file:
```bash
vim ~/.config/smart-cmd/config.json
```

The configuration file structure:
```json
{
  "llm": {
    "provider": "openai",
    "model": "gpt-4.1-nano",
    "endpoint": "https://api.openai.com/v1/chat/completions"
  },
  "trigger_key": "ctrl+o",
  "enable_proxy_mode": true,
  "providers": {
    "openai": {
      "endpoint": "https://api.openai.com/v1/chat/completions",
      "model": "gpt-4.1-nano"
    },
    "gemini": {
      "endpoint": "https://generativelanguage.googleapis.com/v1beta/models/",
      "model": "gemini-2.0-flash"
    },
    "openrouter": {
      "endpoint": "https://openrouter.ai/api/v1/chat/completions",
      "model": "qwen/qwen3-coder:free"
    }
  }
}
```

**Configuration Options:**
- **`enable_proxy_mode`**: `true` for daemon mode, `false` for basic mode
- **`trigger_key`**: Keyboard shortcut (default: "ctrl+o")
- **`llm.provider`**: LLM provider (openai, gemini, openrouter)
- **`llm.model`**: Model name to use
- **`llm.endpoint`**: API endpoint URL

## Troubleshooting

### Common Issues

**"Command not found: smart-cmd" or "smart-cmd-completion"**
```bash
# Check if ~/.local/bin is in your PATH
echo $PATH | grep -q "$HOME/.local/bin" || echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verify installation
which smart-cmd
which smart-cmd-completion
which smart-cmd-daemon
```

**"Permission denied" errors**
```bash
# Fix permissions for all smart-cmd components
chmod +x ~/.local/bin/smart-cmd
chmod +x ~/.local/bin/smart-cmd-completion
chmod +x ~/.local/bin/smart-cmd-daemon
chmod +x ~/.local/bin/smart-cmd.bash
```

## Installation Scripts

### install.sh

The `install.sh` script automates the entire setup process:

- **Build Project**: Uses nob.h build system to compile all components 
- **Install Components**: Copies all necessary files to `~/.local/bin/`:
  - `smart-cmd`: Main utility program
  - `smart-cmd-completion`: Fallback completion backend
  - `smart-cmd-daemon`: Daemon with PTY support
  - `smart-cmd.bash`: Bash integration script
- **Set Permissions**: Adds executable permissions to all files
- **Configuration**: Creates `~/.config/smart-cmd/` directory and installs config file
- **Shell Integration**: Automatically adds smart-cmd integration to `~/.bashrc`
- **System Service**: Optionally installs systemd user service for auto-starting daemon

### uninstall.sh

The `uninstall.sh` script safely removes all installed components:

- **Stop Daemon**: Safely stops any running smart-cmd-daemon
- **Remove Service**: Deletes systemd user service (if installed)
- **Delete Files**: Removes all smart-cmd components from `~/.local/bin/`
- **Clean Configuration**: Removes smart-cmd integration from `~/.bashrc` (creates backup)
- **Temp Files**: Cleans up daemon temporary files in `/tmp/`
- **Config Directory**: Optionally removes `~/.config/smart-cmd/` configuration directory
- **Process Cleanup**: Checks and cleans up any remaining smart-cmd processes

