#!/bin/bash

# Smart Command Completion for Bash
# Integrates with smart-cmd to provide AI-powered command completions

# Global variables for managing suggestions
_SMART_CMD_SUGGESTIONS=()
_SMART_CMD_CURRENT_INDEX=0
_SMART_CMD_ORIGINAL_LINE=""
_SMART_CMD_ENABLED=1
_SMART_CMD_CURRENT_SUGGESTION=""
_SMART_CMD_SHOWING_HINT=0

# Path to smart-cmd completion binary
_SMART_CMD_BIN="${SMART_CMD_BIN:-$HOME/.local/bin/smart-cmd-completion}"

# Enable/disable smart completion
smart-cmd-toggle() {
    if [[ $_SMART_CMD_ENABLED -eq 1 ]]; then
        _SMART_CMD_ENABLED=0
        echo "Smart completion disabled"
    else
        _SMART_CMD_ENABLED=1
        echo "Smart completion enabled"
    fi
}

# Get command context and call smart-cmd backend
_smart-cmd-get-suggestions() {
    local current_line="$1"
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
    # Only setup bindings if in interactive mode and readline is available
    if [[ $- == *i* ]] && command -v bind >/dev/null 2>&1; then
        # Bind Ctrl+O to trigger smart completion
        bind -x '"\C-o": _smart-cmd-complete'

        # Bind right arrow to accept hint
        bind -x '"\e[C": _smart-cmd-accept-hint'

        # Bind ESC key to clear hint
        bind -x '"\e": _smart-cmd-clear-hint'

        # Simple clear hint function for other operations
        bind 'set completion-ignore-case on' 2>/dev/null || true
    fi
}

# Auto-setup when script is sourced
if [[ -n "$BASH_VERSION" ]]; then
    _smart-cmd-setup
fi