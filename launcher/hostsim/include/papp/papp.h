/* The SDK's header for a build on this Mac: the same declarations, less what only the board's linker understands. */
#ifdef __cplusplus
#define _Static_assert static_assert
#endif
#include PAPP_SDK_HEADER      /* the launcher's own papp.h, wherever its clone is: the build passes the path */
#undef PAPP_MANIFEST
#define PAPP_MANIFEST(...) const papp_manifest_t __papp_manifest = { .magic = PAPP_MANIFEST_MAGIC, .sdk_version = PAPP_SDK_VERSION, __VA_ARGS__ }
#undef PAPP_FAST
#undef PAPP_FAST_DATA
#undef PAPP_FAST_BSS
#define PAPP_FAST
#define PAPP_FAST_DATA
#define PAPP_FAST_BSS
