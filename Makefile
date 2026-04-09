CC      = cc
CFLAGS  = -O2 -std=c99 -Wall -Wextra -pedantic
LDFLAGS = -lX11 -lXinerama
BIN     = nvwm
SRC     = nvwm.c

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS)

install: $(BIN)
	install -Dm755 $(BIN)       /usr/local/bin/$(BIN)
	install -Dm644 config.conf  /etc/nvwm/config.conf

uninstall:
	rm -f /usr/local/bin/$(BIN)
	rm -f /etc/nvwm/config.conf

clean:
	rm -f $(BIN) viwm

.PHONY: install uninstall clean
