/*
 * A batman-adv plugin for collectd.
 *
 * Author: Aras Ergus <alpha.ergus@gmail.com>
 * License: CC0 (http://creativecommons.org/publicdomain/zero/1.0/)
 */

#include "common.h"
#include "plugin.h"

#include <stdlib.h>
#include <time.h>

#define BATCTL_CMD "batctl o"

/* skips all characters until the first character of
 * the next line and return that character
 */
static char first_of_next_line(FILE *f) {
  char c;
  while ((c = fgetc(f)) != '\n' && c != EOF);
  return ungetc(fgetc(f), f);
}

/* converts 6 fields of a MAC address into a single long long int */
static long long unsigned blocks_to_llu(unsigned *bs) {
  long long unsigned n = bs[0];
  unsigned i;
  for (i = 1; i < 6; i++) {
    n *= 256;
    n += bs[i];
  }
  return n;
}

static int batman_read(void) {
  FILE *batctl_out = popen(BATCTL_CMD, "r");

  if (batctl_out != NULL) {
    char c;
    char node_mac[13];
    unsigned blocks[6];
    float last_seen;
    unsigned quality;
    unsigned hop_blocks[6];

    cdtime_t measuring_time = cdtime();

    /* We are not interested in the first two lines of the output. */
    first_of_next_line(batctl_out);
    while ((c = first_of_next_line(batctl_out)) != EOF) {
      value_t values[3];
      value_list_t vl = VALUE_LIST_INIT;

      /* Each other line contains the fields
       * originator, last seen, (#/255), next hop,
       * [outgoing IF] and potential next hops.
       * We currently only collect information
       * from the former four fields.
       */ 
      if (fscanf(batctl_out,
                 "%x:%x:%x:%x:%x:%x %fs (%u) %x:%x:%x:%x:%x:%x",
                 blocks, blocks + 1, blocks + 2,
                 blocks + 3, blocks + 4, blocks + 5,
                 &last_seen,  &quality,
                 hop_blocks, hop_blocks + 1, hop_blocks + 2,
                 hop_blocks + 3, hop_blocks + 4, hop_blocks + 5) ==
          14) {

        values[0].gauge = (gauge_t) last_seen;
        values[1].absolute = (absolute_t) quality;
        values[2].absolute = (absolute_t) blocks_to_llu(hop_blocks);
        vl.values_len = 3;
        vl.time = measuring_time;
        sprintf(node_mac, "%llx", blocks_to_llu(blocks));
        sstrncpy(vl.host, hostname_g, sizeof(vl.host));
        sstrncpy(vl.plugin, "batman_adv", sizeof(vl.plugin));
        sstrncpy(vl.type, "batman_adv_origt", sizeof(vl.type));
        sstrncpy(vl.type_instance, node_mac, sizeof(vl.type_instance));
        vl.values = values;
        plugin_dispatch_values(&vl);

        /* A day may come when we have to debug this plugin again.
         * printf("%llx\t%f\t%llu\t%llx\n",
         *        blocks_to_llu(blocks),
         *        values[0].gauge,
         *        values[1].absolute,
         *        values[2].absolute);
         */
      }
      else {
        return -1;
      }
    }
    if (pclose(batctl_out) == -1) {
      return -1;
    }
  }
  else {
    return -1;
  }

  return 0;
}

void module_register(void) {
  plugin_register_read("batman_adv", batman_read);
}
