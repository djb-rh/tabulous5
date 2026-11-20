#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
int papp_main(int argc, char **argv);
FILE *host_fopen(const char *path, const char *mode);
#ifdef __cplusplus
}
#endif
#define main papp_main
#define fopen host_fopen
