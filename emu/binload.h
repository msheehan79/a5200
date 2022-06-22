#ifndef _BINLOAD_H_
#define _BINLOAD_H_

#include <stdio.h> /* FILE */
#include "atari.h" /* UBYTE */

extern FILE *bin_file;

int BIN_loader(const char *filename);
extern int start_binloading;
int BIN_loader_start(UBYTE *buffer);

#endif /* _BINLOAD_H_ */
