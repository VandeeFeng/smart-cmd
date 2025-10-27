#!/bin/bash

# Smart Command Completion for Bash
# Integrates with smart-cmd daemon to provide AI-powered command completions

# Global variables for managing suggestions
_SMART_CMD_SUGGESTIONS=()
_SMART_CMD_CURRENT_INDEX=0
_SMART_CMD_ORIGINAL_LINE=""
_SMART_CMD_ENABLED=1
_SMART_CMD_CURRENT_SUGGESTION=""
_SMART_CMD_SHOWING_HINT=0

# Configuration variables
_SMART_CMD_PROXY_MODE=0
_SMART_CMD_SHOW_STARTUP_MESSAGES=1
_SMART_CMD_CONFIG_FILE="$HOME/.config/smart-cmd/config.json"

# Daemon-related variables
_SMART_CMD_DAEMON_PID=0
_SMART_CMD_DAEMON_SOCKET=""
_SMART_CMD_DAEMON_SESSION=""
_SMART_CMD_DAEMON_AUTO_START=1

# Paths
_SMART_CMD_BIN="${SMART_CMD_BIN:-$HOME/.local/bin/smart-cmd-completion}"
_SMART_CMD_DAEMON_BIN="${SMART_CMD_DAEMON_BIN:-$HOME/.local/bin/smart-cmd-daemon}"

# Configuration functions
_smart-cmd-load-config() {
    if [[ ! -f "$_SMART_CMD_CONFIG_FILE" ]]; then
        _SMART_CMD_PROXY_MODE=0
        _SMART_CMD_SHOW_STARTUP_MESSAGES=1
        return 0
    fi

    # Try to parse JSON for enable_proxy_mode and show_startup_messages
    if command -v python3 >/dev/null 2>&1; then
        local config_result=$(python3 -c "
import json, sys
try:
    with open('$_SMART_CMD_CONFIG_FILE', 'r') as f:
        config = json.load(f)
    proxy_mode = config.get('enable_proxy_mode', False)
    show_messages = config.get('show_startup_messages', True)
    print(f'{1 if proxy_mode else 0} {1 if show_messages else 0}')
except:
    print('0 1')
" 2>/dev/null)
        _SMART_CMD_PROXY_MODE="${config_result%% *}"
        _SMART_CMD_SHOW_STARTUP_MESSAGES="${config_result##* }"
    elif command -v jq >/dev/null 2>&1; then
        local proxy_mode=$(jq -r '.enable_proxy_mode // false' "$_SMART_CMD_CONFIG_FILE" 2>/dev/null)
        local show_messages=$(jq -r '.show_startup_messages // true' "$_SMART_CMD_CONFIG_FILE" 2>/dev/null)
        [[ "$proxy_mode" == "true" ]] && _SMART_CMD_PROXY_MODE=1 || _SMART_CMD_PROXY_MODE=0
        [[ "$show_messages" == "true" ]] && _SMART_CMD_SHOW_STARTUP_MESSAGES=1 || _SMART_CMD_SHOW_STARTUP_MESSAGES=0
    else
        # Fallback: simple grep (less reliable)
        if grep -q '"enable_proxy_mode"[[:space:]]*:[[:space:]]*true' "$_SMART_CMD_CONFIG_FILE" 2>/dev/null; then
            _SMART_CMD_PROXY_MODE=1
        else
            _SMART_CMD_PROXY_MODE=0
        fi
        if grep -q '"show_startup_messages"[[:space:]]*:[[:space:]]*false' "$_SMART_CMD_CONFIG_FILE" 2>/dev/null; then
            _SMART_CMD_SHOW_STARTUP_MESSAGES=0
        else
            _SMART_CMD_SHOW_STARTUP_MESSAGES=1
        fi
    fi
}


# Daemon management functions
_smart-cmd-find-daemon() {
    # Look for running daemon by checking lock files
    local tmp_dir="${TMPDIR:-/tmp}"
    local lock_files=("$tmp_dir"/smart-cmd.lock.*)

    for lock_file in "${lock_files[@]}"; do
        if [[ -f "$lock_file" ]]; then
            local pid=$(cat "$lock_file" 2>/dev/null)
            if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
                _SMART_CMD_DAEMON_PID="$pid"
                # Find corresponding socket file
                local socket_file="${lock_file/lock/socket}"
                if [[ -S "$socket_file" ]]; then
                    _SMART_CMD_DAEMON_SOCKET="$socket_file"
                    # Extract session ID from filename
                    _SMART_CMD_DAEMON_SESSION=$(basename "$socket_file" | sed 's/.*\.//')
                    return 0
                fi
            fi
        fi
    done

    return 1
}

_smart-cmd-start-daemon() {
    if [[ $_SMART_CMD_DAEMON_AUTO_START -ne 1 ]]; then
        return 1
    fi

    if [[ ! -x "$_SMART_CMD_DAEMON_BIN" ]]; then
        echo "Warning: smart-cmd-daemon not found at $_SMART_CMD_DAEMON_BIN" >&2
        return 1
    fi

    # Start daemon in background
    "$_SMART_CMD_DAEMON_BIN" >/dev/null 2>&1 &
    local daemon_pid=$!
    disown $daemon_pid 2>/dev/null || true

    # Give daemon time to start
    local attempts=10
    while [[ $attempts -gt 0 ]]; do
        if _smart-cmd-find-daemon; then
            if [[ $_SMART_CMD_SHOW_STARTUP_MESSAGES -eq 1 ]]; then
                echo "✓ Daemon started (PID: $_SMART_CMD_DAEMON_PID, Session: $_SMART_CMD_DAEMON_SESSION)" >&2
            fi
            return 0
        fi
        sleep 0.5
        ((attempts--))
    done

    echo "Failed to start daemon" >&2
    return 1
}

# Start daemon silently (for completion)
_smart-cmd-start-daemon-silent() {
    # Check if auto-start is enabled
    if [[ $_SMART_CMD_DAEMON_AUTO_START -ne 1 ]]; then
        return 1
    fi

    if [[ ! -x "$_SMART_CMD_DAEMON_BIN" ]]; then
        return 1
    fi

    # Start daemon in background silently
    "$_SMART_CMD_DAEMON_BIN" >/dev/null 2>&1 &
    local daemon_pid=$!

    # Give daemon time to start
    local attempts=10
    while [[ $attempts -gt 0 ]]; do
        if _smart-cmd-find-daemon; then
            return 0
        fi
        sleep 0.5
        ((attempts--))
    done

    return 1
}

_smart-cmd-stop-daemon() {
    if [[ $_SMART_CMD_DAEMON_PID -gt 0 ]]; then
        if kill -0 "$_SMART_CMD_DAEMON_PID" 2>/dev/null; then
            "$_SMART_CMD_DAEMON_BIN" --stop >/dev/null 2>&1
            _SMART_CMD_DAEMON_PID=0
            _SMART_CMD_DAEMON_SOCKET=""
            _SMART_CMD_DAEMON_SESSION=""
            echo "Daemon stopped" >&2
            return 0
        fi
    fi
    return 1
}


_smart-cmd-ensure-daemon() {
    local silent="${1:-0}"  # Default to not silent

    # Check if daemon mode is enabled
    if [[ $_SMART_CMD_PROXY_MODE -ne 1 ]]; then
        return 1  # Daemon mode not enabled
    fi

    # First try to find existing daemon
    if _smart-cmd-find-daemon; then
        return 0  # Daemon is running
    fi

    # If not found, try to start it
    if [[ $silent -eq 1 ]]; then
        _smart-cmd-start-daemon-silent
    else
        _smart-cmd-start-daemon
    fi
}

# Enable/disable smart completion
smart-cmd-toggle() {
    if [[ $_SMART_CMD_ENABLED -eq 1 ]]; then
        _SMART_CMD_ENABLED=0
        echo "Smart completion disabled"
    else
        _SMART_CMD_ENABLED=1
        echo "Smart completion enabled"
        # Try to ensure daemon is running when enabling
        _smart-cmd-ensure-daemon
    fi
}

# Additional daemon management commands
smart-cmd-start() {
    _smart-cmd-start-daemon
}

smart-cmd-stop() {
    _smart-cmd-stop-daemon
}

smart-cmd-status() {
    # Load current configuration
    _smart-cmd-load-config

    echo "Current mode: $([[ $_SMART_CMD_PROXY_MODE -eq 1 ]] && echo "DAEMON (PTY mode)" || echo "BASIC (direct AI)")"

    if [[ $_SMART_CMD_PROXY_MODE -eq 1 ]]; then
        if _smart-cmd-find-daemon; then
            echo "Daemon is running (PID: $_SMART_CMD_DAEMON_PID)"
            echo "Session: $_SMART_CMD_DAEMON_SESSION"
            echo "Socket: $_SMART_CMD_DAEMON_SOCKET"
            echo "Status: Running"
        else
            echo "Daemon is not running (will start on demand)"
        fi
    else
        echo "Daemon mode is disabled in configuration"
    fi
}

smart-cmd-mode() {
    # Load current configuration
    _smart-cmd-load-config

    echo "Smart-cmd configuration:"
    echo "  Mode: $([[ $_SMART_CMD_PROXY_MODE -eq 1 ]] && echo "DAEMON (PTY context + command history)" || echo "BASIC (direct AI completion)")"
    echo "  Config file: $_SMART_CMD_CONFIG_FILE"
    echo "  Smart completion: $([[ $_SMART_CMD_ENABLED -eq 1 ]] && echo "enabled" || echo "disabled")"

    if [[ $_SMART_CMD_PROXY_MODE -eq 1 ]]; then
        echo ""
        echo "Daemon features:"
        echo "  - PTY isolation for security"
        echo "  - Command history (last 50 commands, 1 hour)"
        echo "  - Context-aware AI suggestions"
        echo "  - Session persistence"
    else
        echo ""
        echo "Basic mode features:"
        echo "  - Direct AI completion"
        echo "  - Environment context (cwd, git, etc.)"
        echo "  - No persistent history"
        echo "  - Faster response time"
    fi
}

# Get command context and call smart-cmd backend
_smart-cmd-get-suggestions() {
    local current_line="$1"

    # Check if daemon mode is enabled
    if [[ $_SMART_CMD_PROXY_MODE -eq 1 ]]; then
        # Daemon mode: check if daemon process exists
        if _smart-cmd-find-daemon; then
            # Use environment variable approach for daemon communication
            export SMART_CMD_DAEMON_REQUEST="suggestion:$PWD:$current_line"
            local result=$("$_SMART_CMD_BIN" "$current_line" 0 2>/dev/null)
            unset SMART_CMD_DAEMON_REQUEST
            if [[ $? -eq 0 && -n "$result" && ! "$result" =~ ^error: ]]; then
                echo "$result"
                return 0
            fi
        fi

        # If daemon mode is enabled but daemon failed, show error
        echo "error:Daemon mode enabled but daemon not available"
        return 1
    else
        # Basic mode: use fallback backend directly
        :  # Continue to basic fallback logic below
    fi

    # Basic fallback mode (or daemon failure)
    local current_dir="$PWD"

    # Prepare context for the backend
    local context="{"
    context+="\"command_line\":\"$current_line\","
    context+="\"cwd\":\"$current_dir\","
    context+="\"user\":\"$USER\","
    context+="\"host\":\"$HOSTNAME\""

    # Add git context if in git repository
    if git rev-parse --git-dir > /dev/null 2>&1; then
        local git_branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
        local git_status=$(git status --porcelain 2>/dev/null | wc -l)
        context+=",\"git\":{\"branch\":\"$git_branch\",\"dirty\":$([ "$git_status" -gt 0 ] && echo "true" || echo "false")}"
    fi

    context+="}"

    # Call the backend
    if [[ -x "$_SMART_CMD_BIN" ]]; then
        local result=$("$_SMART_CMD_BIN" --input "$current_line" --context "$context" 2>/dev/null)
        if [[ $? -eq 0 && -n "$result" ]]; then
            echo "$result"
        fi
    fi
}

# Show hint below the current line
_smart-cmd-show-hint() {
    if [[ -n "$_SMART_CMD_CURRENT_SUGGESTION" && $_SMART_CMD_SHOWING_HINT -eq 1 ]]; then
        local current_line="${READLINE_LINE}"
        local suggestion_type="${_SMART_CMD_CURRENT_SUGGESTION:0:1}"
        local suggestion_text="${_SMART_CMD_CURRENT_SUGGESTION:1}"
        # Remove leading spaces from suggestion text for display
        suggestion_text=$(echo "$suggestion_text" | sed 's/^ *//')

        # Save cursor position and show hint on next line
        tput sc
        # Move to end of current line and go to next line
        tput el  # Clear to end of line
        echo
        case "$suggestion_type" in
            "+")
                # Show completion hint - show current input -> completed command
                echo -e "\e[90m💡 Suggestion: $current_line -> $suggestion_text\e[0m"
                echo -e "\e[90m→ Press Right arrow to accept, ESC to dismiss\e[0m"
                ;;
            "=")
                # Show new command hint
                echo -e "\e[90m💡 Suggestion: $current_line -> $suggestion_text\e[0m"
                echo -e "\e[90m→ Press Right arrow to accept, ESC to dismiss\e[0m"
                ;;
        esac
        tput rc  # Restore cursor position
        # Redraw current line to ensure it's visible
        tput el
        echo -n "$current_line"
        # Move cursor back to original position
        tput cub $(( ${#current_line} - READLINE_POINT ))
    fi
}

# Accept the current hint
_smart-cmd-accept-hint() {
    if [[ -n "$_SMART_CMD_CURRENT_SUGGESTION" && $_SMART_CMD_SHOWING_HINT -eq 1 ]]; then
        local current_line="${READLINE_LINE}"
        local suggestion_type="${_SMART_CMD_CURRENT_SUGGESTION:0:1}"
        local suggestion_text="${_SMART_CMD_CURRENT_SUGGESTION:1}"

        # Clear the hint display first
        tput sc
        tput ed  # Clear to end of screen
        tput rc

        # Clear the current line and move to beginning
        tput el1  # Clear from beginning to cursor
        tput cr   # Carriage return

        case "$suggestion_type" in
            "+")
                # Accept completion: apply the completion (which may replace or append)
                # Remove leading space from suggestion text
                local clean_suggestion=$(echo "$suggestion_text" | sed 's/^ *//')
                READLINE_LINE="$clean_suggestion"
                READLINE_POINT=$((${#READLINE_LINE}))
                # Display the new line
                echo -n "$READLINE_LINE"
                ;;
            "=")
                # Accept new command: replace current line
                # Remove leading space from suggestion text
                local clean_suggestion=$(echo "$suggestion_text" | sed 's/^ *//')
                READLINE_LINE="$clean_suggestion"
                READLINE_POINT=$((${#READLINE_LINE}))
                # Display the new line
                echo -n "$READLINE_LINE"
                ;;
        esac

        # Clear the hint state
        _SMART_CMD_CURRENT_SUGGESTION=""
        _SMART_CMD_SHOWING_HINT=0

        # Move cursor to correct position
        tput cub $(( ${#READLINE_LINE} - READLINE_POINT ))
    else
        # No hint to accept, just behave as normal right arrow
        if [[ $READLINE_POINT -lt ${#READLINE_LINE} ]]; then
            READLINE_POINT=$((READLINE_POINT + 1))
        fi
    fi
}

# Clear hint when input changes
_smart-cmd-clear-hint() {
    if [[ $_SMART_CMD_SHOWING_HINT -eq 1 ]]; then
        # Clear the hint display
        tput sc
        tput ed
        tput rc
        _SMART_CMD_CURRENT_SUGGESTION=""
        _SMART_CMD_SHOWING_HINT=0
    fi
}

# Main completion function triggered by Ctrl+O
_smart-cmd-complete() {
    # Check if smart completion is enabled
    if [[ $_SMART_CMD_ENABLED -eq 0 ]]; then
        return 0
    fi

    local current_line="${READLINE_LINE}"

    # Clear any existing hint
    _smart-cmd-clear-hint

    # Get suggestions from backend
    local suggestions_json=$(_smart-cmd-get-suggestions "$current_line")

    if [[ -n "$suggestions_json" ]]; then
        # Parse JSON response and get first suggestion
        local first_suggestion=""

        # Extract first suggestion using Python or jq if available
        if command -v python3 >/dev/null 2>&1; then
            first_suggestion=$(echo "$suggestions_json" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    suggestions = data.get('suggestions', [])
    if suggestions:
        print(suggestions[0])
except:
    pass
")
        elif command -v jq >/dev/null 2>&1; then
            first_suggestion=$(echo "$suggestions_json" | jq -r '.suggestions[0]' 2>/dev/null)
        else
            # Fallback to simple text parsing
            local suggestions_part=$(echo "$suggestions_json" | sed 's/.*"suggestions":\s*\[\(.*\)\].*/\1/')
            local first_item=$(echo "$suggestions_part" | cut -d',' -f1)
            first_suggestion=$(echo "$first_item" | sed 's/^"\(.*\)"$/\1/')
        fi

        if [[ -n "$first_suggestion" ]]; then
            _SMART_CMD_CURRENT_SUGGESTION="$first_suggestion"
            _SMART_CMD_SHOWING_HINT=1

            # Show the hint
            _smart-cmd-show-hint
        fi
    fi
}

# Setup key binding
_smart-cmd-setup() {
    # Load configuration first (needed for both interactive and non-interactive modes)
    _smart-cmd-load-config

    # Only setup bindings if in interactive mode and readline is available
    if [[ $- == *i* ]] && command -v bind >/dev/null 2>&1; then

        # Display current mode (if enabled)
        if [[ $_SMART_CMD_SHOW_STARTUP_MESSAGES -eq 1 ]]; then
            echo "Smart-cmd enabled" >&2
        fi

        # Try to find existing daemon or start it (only if daemon mode)
        _smart-cmd-ensure-daemon

        # Bind Ctrl+O to trigger smart completion
        bind -x '"\C-o": _smart-cmd-complete'

        # Bind right arrow to accept hint
        bind -x '"\e[C": _smart-cmd-accept-hint'

        # Bind ESC key to clear hint
        bind -x '"\e": _smart-cmd-clear-hint'

        # Simple clear hint function for other operations
        bind 'set completion-ignore-case on' 2>/dev/null || true

        # Setup completion on shell exit to clean up daemon
        trap '_smart-cmd-cleanup' EXIT
    fi
}

# Cleanup function
_smart-cmd-cleanup() {
    # Optionally stop daemon on shell exit (disabled by default)
    # Users can enable by setting SMART_CMD_DAEMON_AUTO_STOP=true
    if [[ "$SMART_CMD_DAEMON_AUTO_STOP" == "true" ]]; then
        _smart-cmd-stop-daemon
    fi
}

# Auto-setup when script is sourced
if [[ -n "$BASH_VERSION" ]]; then
    _smart-cmd-setup
fi