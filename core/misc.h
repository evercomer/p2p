#ifndef __misc_h__
#define __misc_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <platform_adpt.h>

#define SAFE_FREE(p) if(p) { free(p); p=NULL; }
#define SAFE_DELETE(p) if(p) { delete p; p=NULL; }

char *strncpyz(char *dst, const char *src, int n);

#ifdef WIN32
#include <io.h>
#define filelength(fd) _filelength(fd)
#elif defined(__LINUX__)
unsigned long filelength(int fd);
#endif

//bin <-> string of IPCAM's key
extern void key2string(const unsigned char key[16], char str[25]);
extern BOOL string2key(const char *str, unsigned char key[16]);

#ifdef __cplusplus
}
#endif

#endif
