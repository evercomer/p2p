#include "platform_adpt.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#ifdef WIN32

//

#elif defined(__LINUX__)

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#endif


int strncpyz(char *dest, const char *s, int n)
{
	int i;
	for(i=0; i<n-1 && s[i]; i++)
	{
		dest[i] = s[i];
	}
	dest[i] = '\0';
	return i;
}

void key2string(const unsigned char key[16], char str[25])
{
	int i, j;
	//0~12
	int ci, bi, val;
	for(i=0, j=0; i<20; i++, j++)
	{
		if(i==4 || i==8 || i==12 || i==16) str[j++] = '-';
		ci = i*5/8;
		bi = (i*5)%8;
		if(bi < 3)
			val = (key[ci] >> (3-bi)) & 0x1F;
		else
			val = ((key[ci] << (bi-3)) | (key[ci+1] >> (11-bi))) & 0x1F;
		if(val <= 9)
			str[j] = '0' + val;
		else
			str[j] = 'A' + val - 10;
	}
	str[24] = '\0';
}

BOOL string2key(const char *str, unsigned char key[16])
{
	int i, j;
	int ci, bi;
	char strkey[25];

	memset(key, 0, 16);
	for(i=0, j=0; str[j]; j++)
	{
		if(isalnum(str[j])) strkey[i++] = toupper(str[j]);
		else if(str[j] != '-') return FALSE;
	}
	strkey[i++] = '\0';
	if(i != 21) return 0;
	for(i=0; i<20; i++)
	{
		if(isdigit(strkey[i])) strkey[i] -= '0';
		else strkey[i] -= ('A' - 10);
	}
	for(i=0; i<13; i++)
	{
		ci = 8*i/5;
		bi = (8*i)%5;
		if(bi<=2)
			key[i] = (strkey[ci] << (bi+3)) | (strkey[ci+1] >> (2-bi));
		else
			key[i] = (strkey[ci] << (bi+3)) | (strkey[ci+1] << (bi-2)) | (strkey[ci+2] >> (7-bi));
	}
	key[12] &= 0xF0;
	return TRUE;
}

#if defined(__LINUX__)
unsigned long filelength(int fno)
{
	struct stat _stat;
	fstat(fno, &_stat);
	return _stat.st_size;
}
#endif
