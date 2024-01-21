/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to the
 * public domain. We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors. We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all present
 * and future rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include	<assert.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>

#include	"netd.h"
#include	"network.h"

network_t *networks = NULL;

struct network *
netfind(char const *name) {
struct network	*network;

	for (network = networks; network; network = network->net_next) {
		if (strcmp(name, network->net_name) == 0)
			return network;
	}

	errno = ESRCH;
	return NULL;
}

struct network *
netcreate(char const *name) {
struct network	*net;

	if (netfind(name) != NULL) {
		errno = EEXIST;
		return NULL;
	}

	if ((net = calloc(1, sizeof(*net))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	net->net_name = strdup(name);
	net->net_next = networks;
	networks = net;

	return net;
}

void
netdelete(network_t *nonnull net) {
	assert(net);

	for (network_t **node = &networks;
	     *node;
	     node = &(*node)->net_next) {
		if (*node == net) {
			*node = (*node)->net_next;
			free(net);
			return;
		}
	}

	panic("netdelete: trying to remove non-existing network");
}
