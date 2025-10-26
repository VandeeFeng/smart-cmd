CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -DVERSION='"1.0.0"'
LIBS = -lutil -lcurl -ljson-c

.PHONY: all clean test completion

all: smart-cmd-completion

completion: smart-cmd-completion

smart-cmd: src/main.c src/config.c src/llm_client.c src/context_collector.c src/pty_proxy.c src/ui_renderer.c src/keyboard.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

smart-cmd-completion: src/completion.c src/config.c src/llm_client.c src/context_collector.c src/pty_proxy.c src/keyboard.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f smart-cmd smart-cmd-completion

test: smart-cmd-completion
	@echo "Testing smart-cmd-completion..."
	@echo "docker -p" | ./smart-cmd-completion --input "docker -p" --context '{"command_line":"docker -p","cwd":"/tmp","user":"test","host":"test"}' || echo "completion test failed"