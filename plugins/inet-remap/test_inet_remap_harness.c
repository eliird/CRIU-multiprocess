/* The inet-remap plugin's unit harness: includes the plugin .c directly so
 * the static parse/rewrite functions are testable (the same pattern the CRIU
 * plugin tests use). Built by test_inet_remap.py with gcc -iquote the CRIU
 * fork's include dir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inet-remap.c"

static int failures;

static void check(int condition, const char *what)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

static uint32_t ip4(const char *text)
{
	struct in_addr a;
	if (inet_pton(AF_INET, text, &a) != 1) {
		fprintf(stderr, "FAIL: bad ip %s\n", text);
		failures++;
		return 0;
	}
	return a.s_addr;
}

int main(void)
{
	/* The map file: old new pairs. */
	const char *map =
		"10.42.12.34 10.42.99.7\n"
		"10.42.12.35 10.42.99.8\n";
	FILE *f = fopen("/tmp/inet-remap-test.map", "w");
	fputs(map, f);
	fclose(f);

	setenv(INET_REMAP_ENV, "/tmp/inet-remap-test.map", 1);
	remap_count = 0;
	check(inet_remap_init(CR_PLUGIN_STAGE__RESTORE) == 0, "init parses the map");
	check(remap_count == 2, "two entries loaded");

	/* src == old: rewritten to new. */
	uint32_t src = ip4("10.42.12.34");
	uint32_t dst = ip4("192.168.1.1");
	remap_count = 2;
	check(inet_remap_update_inetsk(AF_INET, 0, &src, &dst) == 0, "a matching src rewrites");
	check(src == ip4("10.42.99.7"), "the src becomes the new pod IP");
	check(dst == ip4("192.168.1.1"), "an unrelated dst is untouched");

	/* dst == old: rewritten. */
	src = ip4("192.168.1.1");
	dst = ip4("10.42.12.35");
	check(inet_remap_update_inetsk(AF_INET, 0, &src, &dst) == 0, "a matching dst rewrites");
	check(dst == ip4("10.42.99.8"), "the dst becomes the new pod IP");
	check(src == ip4("192.168.1.1"), "an unrelated src is untouched");

	/* Neither matches: -ENOTSUP, nothing rewritten. */
	src = ip4("192.168.50.1");
	dst = ip4("192.168.50.2");
	check(inet_remap_update_inetsk(AF_INET, 0, &src, &dst) == -ENOTSUP,
	      "no match returns -ENOTSUP");
	check(src == ip4("192.168.50.1") && dst == ip4("192.168.50.2"),
	      "unmatched addresses are untouched");

	/* IPv6: v4-mapped rewrites, otherwise -ENOTSUP. */
	uint32_t v6src[4] = {0, 0, htonl(0xffff), ip4("10.42.12.34")};
	uint32_t v6dst[4] = {0, 0, 0, 0};
	check(inet_remap_update_inetsk(AF_INET6, 0, v6src, v6dst) == 0,
	      "a v4-mapped v6 src rewrites");
	check(v6src[3] == ip4("10.42.99.7"), "the v4-mapped address becomes the new IP");
	uint32_t v6src2[4] = {0x11, 0x22, 0x33, 0x44};
	check(inet_remap_update_inetsk(AF_INET6, 0, v6src2, v6dst) == -ENOTSUP,
	      "a non-v4-mapped v6 returns -ENOTSUP");

	/* Malformed map lines fail the init. */
	f = fopen("/tmp/inet-remap-bad.map", "w");
	fputs("10.42.12.34 not-an-ip\n", f);
	fclose(f);
	setenv(INET_REMAP_ENV, "/tmp/inet-remap-bad.map", 1);
	remap_count = 0;
	check(inet_remap_init(CR_PLUGIN_STAGE__RESTORE) != 0, "a malformed map fails init");

	/* No env: no remap, init succeeds. */
	unsetenv(INET_REMAP_ENV);
	remap_count = 0;
	check(inet_remap_init(CR_PLUGIN_STAGE__RESTORE) == 0, "no env succeeds");
	check(remap_count == 0, "no env loads nothing");

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("inet-remap plugin unit harness: all checks passed\n");
	return 0;
}
