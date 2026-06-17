PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
SYSCONFDIR ?= /etc

CC       ?= cc
CPPFLAGS ?=
CFLAGS   ?= -Os -std=c99 -Wall -Wextra -pedantic
LDFLAGS  ?=
LDLIBS   ?= -lX11 -lXinerama -lXrandr -lXcomposite -lXrender

BIN      = nvwm
SRC      = nvwm.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS) $(LDLIBS)

install: $(BIN)
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(SYSCONFDIR)/nvwm
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -m 644 config.conf $(DESTDIR)$(SYSCONFDIR)/nvwm/config.conf

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(SYSCONFDIR)/nvwm/config.conf

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
