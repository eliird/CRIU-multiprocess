/*
 * inet-remap — a CRIU plugin that remaps IPv4 socket addresses at restore.
 *
 * Ported from dynamo-snapshot's
 * agent/plugins/inet-remap/snapshot_inet_remap.c (Apache-2.0; NVIDIA
 * CORPORATION & AFFILIATES), adapted to this repository's CRIU fork hook
 * names (CR_PLUGIN_HOOK__UPDATE_INETSK, present since upstream commit
 * 5b3a75409; criu/sk-inet.c calls it for every inet socket at restore with
 * the image's src/dst addresses).
 *
 * The map file is an "old new" IPv4 pair per line ('#' comments). For every
 * restored inet socket whose recorded source or destination equals `old`,
 * the address is rewritten to `new`; everything else returns -ENOTSUP (the
 * fork's run_plugins ignores it). This is the restore-time layer on top of
 * the loopback-at-capture rule: sockets bound to the capture pod's IP —
 * which does not exist in the restore pod — are recreated on the restore
 * pod's IP.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string.h>
#include "log.h"
#include "criu-plugin.h"

#define INET_REMAP_ENV "CRIU_INET_REMAP_MAP"
#define INET_REMAP_MAX 256
#define log_error(fmt, ...) pr_err("inet-remap: " fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) pr_info("inet-remap: " fmt, ##__VA_ARGS__)

struct inet_remap_entry {
	uint32_t old_addr;
	uint32_t new_addr;
};

static struct inet_remap_entry remaps[INET_REMAP_MAX];
static size_t remap_count;

static int parse_mapping_line(const char *path, unsigned int line_no, char *line)
{
	char old_ip[16], new_ip[16], extra;
	struct in_addr old_addr, new_addr;
	int matched;

	matched = sscanf(line, "%15s %15s %c", old_ip, new_ip, &extra);
	if (matched != 2) {
		log_error("invalid line %u in %s\n", line_no, path);
		return -1;
	}

	if (remap_count == INET_REMAP_MAX) {
		log_error("too many mappings in %s, max %u\n", path, INET_REMAP_MAX);
		return -1;
	}

	if (inet_pton(AF_INET, old_ip, &old_addr) != 1) {
		log_error("invalid old IPv4 address %s in %s:%u\n", old_ip, path, line_no);
		return -1;
	}
	if (inet_pton(AF_INET, new_ip, &new_addr) != 1) {
		log_error("invalid new IPv4 address %s in %s:%u\n", new_ip, path, line_no);
		return -1;
	}

	remaps[remap_count].old_addr = old_addr.s_addr;
	remaps[remap_count].new_addr = new_addr.s_addr;
	remap_count++;
	return 0;
}

static int load_remap_file(const char *path)
{
	FILE *file;
	char line[256];
	unsigned int line_no = 0;

	file = fopen(path, "r");
	if (file == NULL) {
		log_error("failed to open %s: %s\n", path, strerror(errno));
		return -1;
	}

	while (fgets(line, sizeof(line), file) != NULL) {
		line_no++;
		if (strchr(line, '\n') == NULL && !feof(file)) {
			log_error("line %u in %s is too long\n", line_no, path);
			fclose(file);
			return -1;
		}
		if (parse_mapping_line(path, line_no, line) != 0) {
			fclose(file);
			return -1;
		}
	}

	if (ferror(file)) {
		log_error("failed while reading %s\n", path);
		fclose(file);
		return -1;
	}

	fclose(file);
	log_info("loaded %zu mapping(s) from %s\n", remap_count, path);
	return 0;
}

static int inet_remap_init(int stage)
{
	const char *path;

	remap_count = 0;
	if (stage != CR_PLUGIN_STAGE__RESTORE)
		return 0;

	path = getenv(INET_REMAP_ENV);
	if (path == NULL || path[0] == '\0') {
		log_info("%s unset; no remap\n", INET_REMAP_ENV);
		return 0;
	}

	return load_remap_file(path);
}

static void inet_remap_exit(int stage, int ret)
{
	(void)stage;
	(void)ret;
	remap_count = 0;
}

static bool rewrite_addr(uint32_t *addr)
{
	size_t i;

	for (i = 0; i < remap_count; i++) {
		if (*addr != remaps[i].old_addr)
			continue;

		*addr = remaps[i].new_addr;
		return true;
	}

	return false;
}

static bool rewrite_v4_mapped_v6_addr(uint32_t *addr)
{
	if (addr[0] != 0 || addr[1] != 0 || addr[2] != htonl(0xffff))
		return false;

	return rewrite_addr(&addr[3]);
}

static int inet_remap_update_inetsk(uint32_t family, uint32_t state, uint32_t *src_ip, uint32_t *dst_ip)
{
	bool changed = false;

	(void)state;

	if (remap_count == 0 || src_ip == NULL || dst_ip == NULL)
		return -ENOTSUP;

	if (family == AF_INET) {
		/* Rewrite ONLY when the recorded address equals the old pod
		 * IP; everything else is untouched. */
		changed |= rewrite_addr(&src_ip[0]);
		changed |= rewrite_addr(&dst_ip[0]);
	} else if (family == AF_INET6) {
		changed |= rewrite_v4_mapped_v6_addr(src_ip);
		changed |= rewrite_v4_mapped_v6_addr(dst_ip);
	} else {
		return -ENOTSUP;
	}

	if (!changed)
		return -ENOTSUP;

	return 0;
}

cr_plugin_desc_t CR_PLUGIN_DESC = {
	.name = "inet_remap",
	.init = inet_remap_init,
	.exit = inet_remap_exit,
	.version = CRIU_PLUGIN_VERSION,
	.max_hooks = CR_PLUGIN_HOOK__MAX,
};

CR_PLUGIN_REGISTER_HOOK(CR_PLUGIN_HOOK__UPDATE_INETSK, inet_remap_update_inetsk)
