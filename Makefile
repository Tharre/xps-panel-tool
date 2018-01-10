.PHONY: all
all: xps-panel-tool

.PHONY: clean
clean:
	rm -f xps-panel-tool

xps-panel-tool: main.c
	$(CC) -std=c11 -g -o $@ $^
