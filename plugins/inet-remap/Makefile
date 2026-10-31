PLUGIN_NAME := inet_remap
PLUGIN_SOBJ := inet-remap.so
PLUGIN_SRCS := inet-remap.c
PLUGIN_HEADERS :=

DEPS_INET_REMAP := $(PLUGIN_SOBJ)

PLUGIN_INCLUDE  	:= -iquote../../include
PLUGIN_INCLUDE  	+= -iquote../../criu/include
PLUGIN_INCLUDE  	+= -iquote../../criu/arch/$(ARCH)/include/
PLUGIN_INCLUDE  	+= -iquote../../

PLUGIN_CFLAGS := -g -Wall -Werror -shared -nostartfiles -fPIC

__nmk_dir ?= ../../scripts/nmk/scripts/
include $(__nmk_dir)msg.mk

all: $(DEPS_INET_REMAP)

inet-remap.so: $(PLUGIN_SRCS) $(PLUGIN_HEADERS)
	$(call msg-gen, $@)
	$(Q) $(CC) $(PLUGIN_CFLAGS) $(DEFINES) $(PLUGIN_SRCS) -o $@ $(PLUGIN_INCLUDE) $(PLUGIN_LDFLAGS)

check:
	$(Q) $(CC) -O0 -g -iquote../../include -iquote../../criu/include -iquote../../ \
	  -o inet-remap-test test_inet_remap_harness.c && ./inet-remap-test

clean:
	$(call msg-clean, $@)
	$(Q) $(RM) $(PLUGIN_SOBJ) inet-remap-test
.PHONY: clean

mrproper: clean

install: $(DEPS_INET_REMAP)
	$(Q) mkdir -p $(DESTDIR)$(PLUGINDIR)
	$(E) "  INSTALL " $(PLUGIN_NAME)
	$(Q) install -m 755 $(PLUGIN_SOBJ) $(DESTDIR)$(PLUGINDIR)
.PHONY: install

uninstall:
	$(E) " UNINSTALL" $(PLUGIN_NAME)
	$(Q) $(RM) $(DESTDIR)$(PLUGINDIR)/$(PLUGIN_SOBJ)
.PHONY: uninstall
