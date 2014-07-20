/*
 * Copyright (c) 2013-2014 Red Hat.
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 */
#include "pmapi.h"
#include "impl.h"
#include "internal.h"
#include "avahi.h"
#include "probe.h"

/*
 * Advertise the given service using all available means. The implementation
 * must support adding and removing individual service specs on the fly.
 * e.g. "pmcd" on port 1234
 */
__pmServerPresence *
__pmServerAdvertisePresence(const char *serviceSpec, int port)
{
    __pmServerPresence *s;

    /* Allocate a server presence and copy the given data. */
    if ((s = malloc(sizeof(*s))) == NULL) {
	__pmNoMem("__pmServerAdvertisePresence: can't allocate __pmServerPresence",
		  sizeof(*s), PM_FATAL_ERR);
    }
    s->serviceSpec = strdup(serviceSpec);
    if (s->serviceSpec == NULL) {
	__pmNoMem("__pmServerAdvertisePresence: can't allocate service spec",
		  strlen(serviceSpec) + 1, PM_FATAL_ERR);
    }
    s->port = port;

    /* Now advertise our presence using all available means. If a particular
     * method is not available or not configured, then the respective call
     * will have no effect. Currently, only Avahi is supported.
     */
    __pmServerAvahiAdvertisePresence(s);
    return s;
}

/*
 * Unadvertise the given service using all available means. The implementation
 * must support removing individual service specs on the fly.
 * e.g. "pmcd" on port 1234
 */
void
__pmServerUnadvertisePresence(__pmServerPresence *s)
{
    /* Unadvertise our presence for all available means. If a particular
     * method is not active, then the respective call will have no effect.
     */
    __pmServerAvahiUnadvertisePresence(s);
    free(s->serviceSpec);
    free(s);
}

/*
 * Service discovery API entry points.
 */
int
pmDiscoverServices(const char *service,
		   const char *mechanism,
		   char ***urls)
{
    return __pmDiscoverServicesWithOptions(service, mechanism, NULL, NULL, urls);
}

int
__pmDiscoverServicesWithOptions(const char *service,
				const char *mechanism,
				const char *options,
				const volatile unsigned *flags,
				char ***urls)
{
    int		numUrls;

    (void)options; /* Temporary */

    /*
     * Attempt to discover the requested service(s) using the requested or
     * all available means.
     * If a particular method is not available or not configured, then the
     * respective call will have no effect.
     */
    *urls = NULL;
    numUrls = 0;
    if (mechanism == NULL) {
	numUrls += __pmAvahiDiscoverServices(service, mechanism, flags, numUrls, urls);
	if (! flags || (*flags & PM_SERVICE_DISCOVERY_INTERRUPTED) != 0)
	    numUrls += __pmProbeDiscoverServices(service, mechanism, flags, numUrls, urls);
    }
    else if (strncmp(mechanism, "avahi", 5) == 0)
	numUrls += __pmAvahiDiscoverServices(service, mechanism, flags, numUrls, urls);
    else if (strncmp(mechanism, "probe", 5) == 0)
	numUrls += __pmProbeDiscoverServices(service, mechanism, flags, numUrls, urls);
    else
	numUrls = -EOPNOTSUPP;

    return numUrls;
}

/* For manually adding a service. Also used by pmDiscoverServices(). */
int
__pmAddDiscoveredService(__pmServiceInfo *info,
			 const volatile unsigned *flags,
			 int numUrls,
			 char ***urls)
{
    const char *protocol = info->protocol;
    char *host = NULL;
    char *url;
    size_t size;
    int isIPv6;
    int port;

    /* If address resolution was requested, then do attempt it. */
    if (flags && (*flags & PM_SERVICE_DISCOVERY_RESOLVE) != 0)
	host = __pmGetNameInfo(info->address);

    /*
     * If address resolution was not requested, or if it failed, then
     * just use the address.
     */
    if (host == NULL) {
	host = __pmSockAddrToString(info->address);
	if (host == NULL) {
	    __pmNoMem("__pmAddDiscoveredService: can't allocate host buffer",
		      0, PM_FATAL_ERR);
	}
    }

    /*
     * Allocate the new entry. We need room for the URL prefix, the
     * address/host and the port. IPv6 addresses require a set of []
     * surrounding the address in order to distinguish the port.
     */
    port = __pmSockAddrGetPort(info->address);
    size = strlen(protocol) + sizeof("://");
    size += strlen(host) + sizeof(":65535");
    if ((isIPv6 = (strchr(host, ':') != NULL)))
	size += 2;
    url = malloc(size);
    if (url == NULL) {
	__pmNoMem("__pmAddDiscoveredService: can't allocate new entry",
		  size, PM_FATAL_ERR);
    }
    if (isIPv6)
	snprintf(url, size, "%s://[%s]:%u", protocol, host, port);
    else
	snprintf(url, size, "%s://%s:%u", protocol, host, port);
    free(host);

    /*
     * Now search the current list for the new entry.
     * Add it if not found. We don't want any duplicates.
     */
    if (__pmStringListFind(url, numUrls, *urls) == NULL)
	numUrls = __pmStringListAdd(url, numUrls, urls);

    free(url);
    return numUrls;
}
