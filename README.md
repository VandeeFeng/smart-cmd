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
```

### Manual Installation

If you prefer manual installation:

```bash
# Build the project
make clean && make

# Install binaries
cp smart-cmd-completion ~/.local/bin/
cp smart-cmd.bash ~/.local/bin/

# Create config directory and install configuration
mkdir -p ~/.config/smart-cmd/
cp example_config.json ~/.config/smart-cmd/config.json

# Set executable permissions
chmod +x ~/.local/bin/smart-cmd-completion
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
## install.sh Script Details

The installation script performs these core operations:

### 1. Build Process
```bash
# Clean previous builds
make clean

# Compile the C binary
gcc -O2 -std=c99 -Wall -Wextra \
    src/main.c src/completion.c src/config.c \
    src/llm_client.c src/context_collector.c \
    src/ui_renderer.c src/keyboard.c \
    -o smart-cmd-completion \
    -lcurl -ljson-c
```

### 2. File Installation
- **Binary Installation**: Copies `smart-cmd-completion` to `~/.local/bin/`
- **Script Installation**: Copies `smart-cmd.bash` to `~/.local/bin/`
- **Configuration Setup**: Creates `~/.config/smart-cmd/` directory
- **Config File**: Installs `example_config.json` as `config.json`

### 3. Permission and Path Setup
- **Executable Permissions**: Sets `+x` on both installed files
- **Bash Integration**: Adds `source ~/.local/bin/smart-cmd.bash` to `~/.bashrc`

## Troubleshooting

### Common Issues

**"Command not found: smart-cmd-completion"**
```bash
# Ensure ~/.local/bin is in your PATH
echo $PATH | grep -q "$HOME/.local/bin" || echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

**"Permission denied" errors**
```bash
# Fix permissions
chmod +x ~/.local/bin/smart-cmd-completion
chmod +x ~/.local/bin/smart-cmd.bash
```

