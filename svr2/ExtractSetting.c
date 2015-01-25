#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ExtractSetting.h"

int ExtractSetting(const char *inifile, const char *section, KEYVAL *kv)
{
	KEYVAL *pkv;
	int cnt = 0;
	char line[400], key[30], val[100];
	FILE *fp;
	
	fp = fopen(inifile, "r");
	if(!fp) return -1;

	pkv = kv;
	while(pkv->sKey)
	{
		pkv->got = 0;
		pkv ++;
	}

	int foundSec = !(section && section[0]);
	while(fgets(line, 400, fp))
	{
		if(!foundSec)
		{
			if(line[0] == '[' && strncmp(section, line+1, strlen(section)) == 0 && line[strlen(section)+1] == ']')
				foundSec = 1;
			continue;
		}
		else if(line[0] == '[')
			break;

		char *p = strchr(line, '=');
		if(!p) continue;
		*p = '\0';
		key[0] = val[0] = '\0';
		sscanf(line, "%s", key);
		sscanf(p+1, "%s", val);
		pkv = kv;
		while(pkv->sKey)
		{
			if(strcmp(pkv->sKey, key) == 0)
			{
				switch(pkv->type)
				{
				case KEYVALTYPE_INT:
					if(pkv->pVal) *((int*)pkv->pVal) = pkv->iVal = atoi(val);
					pkv->got = 1;
					cnt ++;
					break;
				case KEYVALTYPE_STRING:
					if(pkv->pVal) 
					{
						*((char*)pkv->pVal + pkv->size - 1) = 0;
						strncpy((char*)pkv->pVal, val, pkv->size-1);
					}
					pkv->got = 1;
					cnt ++;
					break;
				}
				break;
			}
			pkv++;
		}
	}
	fclose(fp);
	return cnt;
}

int ReadSection(const char *inifile, const char *section, char *buff, int size)
{
	char line[400], key[30], val[100];
	FILE *fp;
	int len;
	
	fp = fopen(inifile, "r");
	if(!fp) return -1;


	int foundSec = !(section && section[0]);
	len = 0;
	while(fgets(line, 400, fp))
	{
		if(!foundSec)
		{
			if(line[0] == '[' && strncmp(section, line+1, strlen(section)) == 0 && line[strlen(section)+1] == ']')
				foundSec = 1;
			continue;
		}
		else if(line[0] == '[')
			break;

		char *p = strchr(line, '=');
		if(!p) continue;
		*p = '\0';
		key[0] = val[0] = '\0';
		sscanf(line, "%s", key);
		if(!key[0] || key[0] == ';' || key[0] == '#') continue;

		sscanf(p+1, "%s", val);
		if(len + strlen(key) + strlen(val) + 2 > size) break;

		len += sprintf(buff+len, "%s=%s", key, val) + 1;
	}
	buff[len++] = '\0';
	if(len == 1) buff[len++] = '\0';

	fclose(fp);
	return len;
}
