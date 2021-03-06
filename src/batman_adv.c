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

#define INIT_SIZE 100
#define ENLARGE(x) ((3*(x))/2)

#define BATCTL_CMD "batctl o"

static long long unsigned *nodes, *hops;
static size_t nodes_size, node_count;

static int batman_init(void) {
  if ((nodes = malloc(INIT_SIZE * sizeof(long long unsigned))) !=
      NULL &&
      (hops = malloc(INIT_SIZE * sizeof(long long unsigned))) !=
      NULL) {
    nodes_size = INIT_SIZE;
    node_count = 0;
    return 0;
  }
  else {
    return -1;
  }
}

static int batman_shutdown(void) {
  free(nodes);
  free(hops);
  return 0;
}

static int add_node(long long unsigned node, long long unsigned hop) {
  if (node_count < nodes_size) {
    nodes[node_count] = node;
    hops[node_count] = hop;
    node_count++;
    return 0;
  }
  else {
    size_t new_size = ENLARGE(nodes_size);
    if ((nodes =
         realloc(nodes, new_size * sizeof(long long unsigned))) !=
        NULL &&
        (hops = realloc(hops, new_size * sizeof(long long unsigned))) !=
        NULL) {
      nodes_size = new_size;
      nodes[node_count] = node;
      hops[node_count] = hop;
      node_count++;
      return 0;
    }
    else {
      return -1;
    }
  }
}

static void batman_log(int severity,
                       const char *message,
                       user_data_t *ud) {
  printf ("batman_adv (%i): %s\n", severity, message);
}

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
      value_list_t vl[3] = {VALUE_LIST_INIT,
                            VALUE_LIST_INIT,
                            VALUE_LIST_INIT};

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
        size_t i;
        long long unsigned blocks_llu = blocks_to_llu(blocks),
                           hop_llu = blocks_to_llu(hop_blocks);

        values[0].gauge = (gauge_t) last_seen;
        sstrncpy(vl[0].type, "origt_seen", sizeof(vl[0].type));
        values[1].absolute = (absolute_t) quality;
        sstrncpy(vl[1].type, "origt_quality", sizeof(vl[1].type));

        sstrncpy(vl[2].type, "origt_hop", sizeof(vl[2].type));
        values[2].gauge = (gauge_t) 0.5;
        for (i = 0; i < node_count; i++) {
          if (nodes[i] == blocks_llu) {
            if (hops[i] == hop_llu) {
              values[2].gauge = (gauge_t) 1.0;
              break;
            }
            else {
              hops[i] = hop_llu;
              values[2].gauge = (gauge_t) 0.0;
              break;
            }
         }
        }
        if (values[2].gauge == (gauge_t) 0.5) {
          if (add_node(blocks_llu, hop_llu) == 0) {
            values[2].gauge = (gauge_t) 1.0;
          }
          else {
            batman_log(LOG_WARNING,
                       "failed to enlarge the array of nodes",
                       NULL);
          }
        }

        sprintf(node_mac, "%llx", blocks_llu);
        for (i = 0; i < 3; i++) {
          vl[i].values_len = 1;
          vl[i].time = measuring_time;
          sstrncpy(vl[i].host, hostname_g, sizeof(vl[i].host));
          sstrncpy(vl[i].plugin, "batman_adv", sizeof(vl[i].plugin));
          sstrncpy(vl[i].type_instance,
                   node_mac,
                   sizeof(vl[i].type_instance));
          vl[i].values = values + i;
          plugin_dispatch_values(vl + i);
        }

        /* A day may come when we have to debug this plugin again.
         * printf("%llx\t%f\t%llx\t%f\n",
         *        blocks_to_llu(blocks),
         *        values[0].gauge,
         *        values[1].absolute,
         *        values[2].gauge);
         */
      }
      else {
        batman_log(LOG_WARNING,
                   "failed to parse a line of the originator table",
                   NULL);
        return -1;
      }
    }
    if (pclose(batctl_out) == -1) {
      batman_log(LOG_WARNING, "failed close pipe stream", NULL);
      /* pclose tends to "fail" with errno ECHILD, but it seems
       * to be safe to ignore the result of pclose. See also
       * https://www.gnu.org/software/libc/manual/html_mono/
       * libc.html#Pipe-to-a-Subprocess
       * return -1;
       */
    }
  }
  else {
    batman_log(LOG_WARNING, "failed to open pipe stream", NULL);
    return -1;
  }

  return 0;
}

void module_register(void) {
  plugin_register_init("batman_adv", batman_init);
  plugin_register_shutdown("batman_adv", batman_shutdown);
  plugin_register_log("batman_adv", batman_log, /* user data = */ NULL);
  plugin_register_read("batman_adv", batman_read);
}
