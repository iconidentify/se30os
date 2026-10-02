#ifndef TELNET_H
#define TELNET_H

#include "term.h"

/* Connect the terminal to host:port; done() runs when the session ends. */
void telnet_open(struct term *t, const char *host, u16 port,
		 void (*done)(struct term *));

#endif
