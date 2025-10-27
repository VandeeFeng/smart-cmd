CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -DVERSION='"1.0.0"'
LIBS = -lutil -lcurl -ljson-c

# Source files
CORE_SOURCES = src/config.c src/llm_client.c src/basic_context.c src/pty_proxy.c src/daemon.c src/ipc.c src/daemon_history.c src/manager.c src/completion.c src/utils.c
MAIN_SOURCES = src/main.c
COMPLETION_SOURCES = src/completion.c
DAEMON_SOURCES = src/smart_cmd_daemon.c

# Header files
HEADERS = src/smart_cmd.h src/defaults.h src/utils.h

.PHONY: all clean test completion daemon install uninstall

all: smart-cmd smart-cmd-completion smart-cmd-daemon

completion: smart-cmd-completion

daemon: smart-cmd-daemon

smart-cmd: $(MAIN_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

smart-cmd-completion: $(COMPLETION_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -DCOMPLETION_BINARY $^ -o $@ $(LIBS)

smart-cmd-daemon: $(DAEMON_SOURCES) $(CORE_SOURCES)
	$(CC) $(CFLAGS) -DDAEMON_BINARY $^ -o $@ $(LIBS)

clean:
	rm -f smart-cmd smart-cmd-completion smart-cmd-daemon
	rm -f /tmp/smart-cmd.* /tmp/smart-cmd-*.log

install: all
	install -d $(HOME)/.local/bin
	install -m 755 smart-cmd $(HOME)/.local/bin/
	install -m 755 smart-cmd-completion $(HOME)/.local/bin/
	install -m 755 smart-cmd-daemon $(HOME)/.local/bin/
	install -d $(HOME)/.config/smart-cmd
	install -m 644 smart-cmd.bash $(HOME)/.config/smart-cmd/
	@echo "Installation complete!"
	@echo "Add the following to your ~/.bashrc:"
	@echo "source ~/.config/smart-cmd/smart-cmd.bash"

uninstall:
	rm -f $(HOME)/.local/bin/smart-cmd
	rm -f $(HOME)/.local/bin/smart-cmd-completion
	rm -f $(HOME)/.local/bin/smart-cmd-daemon
	rm -rf $(HOME)/.config/smart-cmd
	@echo "Uninstallation complete!"

test: smart-cmd-completion smart-cmd-daemon
	@echo "Testing smart-cmd main commands..."
	@./smart-cmd status || echo "status test failed"
	@./smart-cmd mode || echo "mode test failed"
	@echo "Testing smart-cmd-completion..."
	@echo "docker -p" | ./smart-cmd-completion --input "docker -p" --context '{"command_line":"docker -p","cwd":"/tmp","user":"test","host":"test"}' || echo "completion test failed"
	@echo "Testing smart-cmd-daemon..."
	@./smart-cmd-daemon --status || echo "daemon status test failed"
	@echo "Testing new management commands..."
	@./smart-cmd toggle || echo "toggle test failed"
	@./smart-cmd toggle || echo "toggle test failed"

test-daemon: smart-cmd-daemon
	@echo "Starting daemon for testing..."
	@./smart-cmd-daemon --debug &
	@sleep 2
	@echo "Testing daemon status..."
	@./smart-cmd-daemon --status
	@echo "Testing daemon ping..."
	@timeout 5 bash -c "echo 'ping' | nc -U /tmp/smart-cmd.socket.* 2>/dev/null || echo 'ping test failed'"
	@echo "Stopping daemon..."
	@./smart-cmd-daemon --stop

security-test: all
	@echo "Running security tests..."
	@echo "Testing environment validation..."
	@env SMART_CMD_DAEMON_ACTIVE=1 ./smart-cmd-daemon 2>/dev/null && echo "FAILED: Nesting prevention test" || echo "PASSED: Nesting prevention"
	@echo "Testing lock file permissions..."
	@./smart-cmd-daemon &
	@sleep 1
	@test -f /tmp/smart-cmd.lock.* && test $(stat -c %a /tmp/smart-cmd.lock.* 2>/dev/null) -eq 644 && echo "PASSED: Lock file permissions" || echo "FAILED: Lock file permissions"
	@./smart-cmd-daemon --stop >/dev/null 2>&1 || true