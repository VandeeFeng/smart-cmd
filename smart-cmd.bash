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

# Configuration and daemon state are now handled by the C binary.

# Paths
_SMART_CMD_BIN="${SMART_CMD_BIN:-$HOME/.local/bin/smart-cmd-completion}"
_SMART_CMD_DAEMON_BIN="${SMART_CMD_DAEMON_BIN:-$HOME/.local/bin/smart-cmd-daemon}"

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

# Additional daemon management commands
smart-cmd-start() {
  "$_SMART_CMD_DAEMON_BIN" --start
}

smart-cmd-stop() {
  "$_SMART_CMD_DAEMON_BIN" --stop
}

smart-cmd-status() {
  "$_SMART_CMD_DAEMON_BIN" --status
}

smart-cmd-mode() {
  "$_SMART_CMD_DAEMON_BIN" --status
}

# Get command context and call smart-cmd backend
_smart-cmd-get-suggestions() {
  local current_line="$1"

  if [[ -x "$_SMART_CMD_BIN" ]]; then
    "$_SMART_CMD_BIN" --input "$current_line" 2>/dev/null
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

    # Use tput to draw the hint without disturbing the current line
    tput sc
    tput el
    echo
    case "$suggestion_type" in
      "+")
        echo -e "\e[90m💡 Suggestion: $current_line -> $suggestion_text\e[0m"
        echo -e "\e[90m→ Press Right arrow to accept, ESC to dismiss\e[0m"
        ;;
      "=")
        echo -e "\e[90m💡 Suggestion: $current_line -> $suggestion_text\e[0m"
        echo -e "\e[90m→ Press Right arrow to accept, ESC to dismiss\e[0m"
        ;;
    esac
    tput rc
    tput el
    echo -n "$current_line"
    tput cub $(( ${#current_line} - READLINE_POINT ))
  fi
}

# Accept the current hint
_smart-cmd-accept-hint() {
  if [[ -n "$_SMART_CMD_CURRENT_SUGGESTION" && $_SMART_CMD_SHOWING_HINT -eq 1 ]]; then
    local current_line="${READLINE_LINE}"
    local suggestion_type="${_SMART_CMD_CURRENT_SUGGESTION:0:1}"
    local suggestion_text="${_SMART_CMD_CURRENT_SUGGESTION:1}"

    # Clear the hint and the current line
    tput sc
    tput ed
    tput rc
    tput el1
    tput cr

    case "$suggestion_type" in
      "+")
        local clean_suggestion=$(echo "$suggestion_text" | sed 's/^ *//')
        READLINE_LINE="$clean_suggestion"
        READLINE_POINT=$((${#READLINE_LINE}))
        echo -n "$READLINE_LINE"
        ;;
      "=")
        local clean_suggestion=$(echo "$suggestion_text" | sed 's/^ *//')
        READLINE_LINE="$clean_suggestion"
        READLINE_POINT=$((${#READLINE_LINE}))
        echo -n "$READLINE_LINE"
        ;;
    esac

    _SMART_CMD_CURRENT_SUGGESTION=""
    _SMART_CMD_SHOWING_HINT=0
    tput cub $(( ${#READLINE_LINE} - READLINE_POINT ))
  else
    if [[ $READLINE_POINT -lt ${#READLINE_LINE} ]]; then
      READLINE_POINT=$((READLINE_POINT + 1))
    fi
  fi
}

# Clear hint when input changes
_smart-cmd-clear-hint() {
  if [[ $_SMART_CMD_SHOWING_HINT -eq 1 ]]; then
    tput sc
    tput ed
    tput rc
    _SMART_CMD_CURRENT_SUGGESTION=""
    _SMART_CMD_SHOWING_HINT=0
  fi
}

# Main completion function triggered by Ctrl+O
_smart-cmd-complete() {
  if [[ $_SMART_CMD_ENABLED -eq 0 ]]; then
    return 0
  fi

  local current_line="${READLINE_LINE}"
  _smart-cmd-clear-hint

  # The C binary now returns the suggestion directly as plain text.
  local first_suggestion=$(_smart-cmd-get-suggestions "$current_line")

  if [[ -n "$first_suggestion" ]]; then
    _SMART_CMD_CURRENT_SUGGESTION="$first_suggestion"
    _SMART_CMD_SHOWING_HINT=1
    _smart-cmd-show-hint
  fi
}

# Setup key binding
_smart-cmd-setup() {
  if [[ $- == *i* ]] && command -v bind >/dev/null 2>&1; then
    echo "Smart-cmd enabled" >&2
    bind -x '"\C-o": _smart-cmd-complete'
    bind -x '"\e[C": _smart-cmd-accept-hint'
    bind -x '"\e": _smart-cmd-clear-hint'
    trap '_smart-cmd-cleanup' EXIT
  fi
}

_smart-cmd-cleanup() {
  if [[ "$SMART_CMD_DAEMON_AUTO_STOP" == "true" ]]; then
    "$_SMART_CMD_DAEMON_BIN" --stop >/dev/null 2>&1
  fi
}

# Auto-setup when script is sourced
if [[ -n "$BASH_VERSION" ]]; then
  _smart-cmd-setup
fi
