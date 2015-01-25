#ifndef __extractsetting_h__
#define __extractsetting_h__

#define KEYVALTYPE_INT		1
#define KEYVALTYPE_STRING	2
typedef struct _tagKeyVal {
	char *sKey;
	int type;
	void *pVal;
	union {
		int size;
		int iVal;
	};
	int got;
} KEYVAL;

#ifdef __cplusplus
extern "C" {
#endif

int ExtractSetting(const char *inifile, const char *section, KEYVAL *kv);
int ReadSection(const char *inifile, const char *sec, char *buff, int size);

#ifdef __cplusplus
}
#endif

#endif
