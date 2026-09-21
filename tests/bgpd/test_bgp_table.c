// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP Routing table range lookup test
 * Copyright (C) 2012 OSR.
 * Copyright (C) 2018 Marcel Röthke (marcel.roethke@haw-hamburg.de), for HAW
 * Hamburg
 *
 * This file is part of FRRouting
 */

#include <zebra.h>

#include "prefix.h"
#include "table.h"
#include "bgpd/bgp_table.h"
#include "linklist.h"

/* Satisfy link requirements from including bgpd.h */
struct zebra_privs_t bgpd_privs = {0};
/*
 * test_node_t
 *
 * Information that is kept for each node in the radix tree.
 */
struct test_node_t {
	/*
	 * Human readable representation of the string.
	 */
	const char *prefix_str;
};

/*
 * add_node
 *
 * Add the given prefix (passed in as a string) to the given table.
 */
static void add_node(struct bgp_table *table, const char *prefix_str, struct test_node_t *tn)
{
	struct prefix_ipv4 p;
	struct bgp_dest *dest;

	assert(prefix_str);

	if (str2prefix_ipv4(prefix_str, &p) <= 0)
		assert(0);

	dest = bgp_node_get(table, (struct prefix *)&p);
	if (dest->info) {
		assert(0);
		return;
	}

	tn->prefix_str = prefix_str;
	dest->info = tn;
}

static bool mark_found(const struct prefix *p, struct prefix *prefix_array, bool *found,
		       size_t prefix_array_size)
{
	bool ret = false;

	for (size_t i = 0; i < prefix_array_size; ++i) {
		if (prefix_same(p, &prefix_array[i])) {
			found[i] = true;
			ret = true;
		}
	}
	return ret;
}

static void check_lookup_result(struct route_node *match, va_list arglist)
{
	char *prefix_str;
	struct prefix *prefixes = NULL;
	bool *found;
	size_t prefix_count = 0;

	while ((prefix_str = va_arg(arglist, char *))) {
		++prefix_count;
		prefixes = realloc(prefixes, sizeof(*prefixes) * prefix_count);

		if (str2prefix(prefix_str, &prefixes[prefix_count - 1]) <= 0)
			assert(0);
	}

	/* check if the result is empty and if it is allowed to be empty */
	if (!match) {
		if (prefix_count) {
			printf("subtree lookup returned nothing, %zu prefixes expected!\n",
			       prefix_count);
			assert(0);
		}
		free(prefixes);
		return;
	}

	found = calloc(prefix_count + 1, sizeof(*found));

	for (struct route_node *rn = match; rn; rn = route_next_until(rn, match)) {
		struct bgp_dest *dest = bgp_dest_from_rnode(rn);
		const struct prefix *dest_p;

		if (!dest)
			continue;

		dest_p = bgp_dest_get_prefix(dest);
		if (bgp_dest_has_bgp_path_info_data(dest) &&
		    !mark_found(dest_p, prefixes, found, prefix_count)) {
			printf("prefix %pFX was not expected!\n", dest_p);
			assert(0);
		}
	}

	for (size_t i = 0; i < prefix_count; ++i) {
		if (!found[i]) {
			printf("prefix %pFX was not found!\n", &prefixes[i]);
			assert(0);
		}
	}

	free(found);
	free(prefixes);
}

static void do_test(struct bgp_table *table, const char *prefix, ...)
{
	va_list arglist;
	struct prefix p;


	va_start(arglist, prefix);
	printf("\nDoing lookup for %s\n", prefix);
	if (str2prefix(prefix, &p) <= 0)
		assert(0);
	struct route_node *rn = bgp_table_subtree_lookup(table, &p);

	check_lookup_result(rn, arglist);

	va_end(arglist);

	printf("Checks successfull\n");
}

/*
 * test_range_lookup
 */
static void test_range_lookup(void)
{
	struct bgp_table *table = bgp_table_init(NULL, AFI_IP, SAFI_UNICAST);

	printf("Testing bgp_table_range_lookup\n");

	printf("Setup bgp_table");
	const char *prefixes[] = {"1.16.0.0/16",   "1.16.128.0/18",
				  "1.16.192.0/18", "1.16.64.0/19",
				  "1.16.160.0/19", "1.16.32.0/20",
				  "1.16.32.0/21",  "16.0.0.0/16"};

	int num_prefixes = array_size(prefixes);
	struct test_node_t tns[num_prefixes];

	for (int i = 0; i < num_prefixes; i++)
		add_node(table, prefixes[i], &tns[i]);

	do_test(table, "1.16.0.0/17", "1.16.64.0/19", "1.16.32.0/20",
		"1.16.32.0/20", "1.16.32.0/21", NULL);
	do_test(table, "1.16.128.0/17", "1.16.128.0/18", "1.16.192.0/18",
		"1.16.160.0/19", NULL);

	do_test(table, "1.16.0.0/16", "1.16.0.0/16", "1.16.128.0/18",
		"1.16.192.0/18", "1.16.64.0/19", "1.16.160.0/19",
		"1.16.32.0/20", "1.16.32.0/21", NULL);

	do_test(table, "1.17.0.0/16", NULL);

	do_test(table, "128.0.0.0/8", NULL);

	do_test(table, "16.0.0.0/8", "16.0.0.0/16", NULL);

	do_test(table, "0.0.0.0/2", "1.16.0.0/16", "1.16.128.0/18",
		"1.16.192.0/18", "1.16.64.0/19", "1.16.160.0/19",
		"1.16.32.0/20", "1.16.32.0/21", "16.0.0.0/16", NULL);

	bgp_table_finish(&table);
}

int main(void)
{
	test_range_lookup();
}
