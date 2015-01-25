#include <stdio.h>
#include <stdarg.h>
#include "platform_adpt.h"
#include "p2plog.h"

static void _getTime(char *strTime)
{
	char sf[16];
#ifdef WIN32
	SYSTEMTIME t;
	GetLocalTime(&t);
	sprintf(sf, "%.06f", t.wMilliseconds/1000.0);
	sprintf(strTime, "%02d:%02d:%02d.%s", t.wHour, t.wMinute, t.wSecond, sf+2);
#else
	struct timeval tv;
	struct tm _tm;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &_tm);
	sprintf(sf, "%.06f", tv.tv_usec/1000000.0);
	sprintf(strTime, "%02d:%02d:%02d.%s", _tm.tm_hour, _tm.tm_min, _tm.tm_sec, sf+2);
#endif
}

static FILE *fpLog = NULL;
void Log(const char *who, const char *fmt, ...)
{
	FILE *fp = fpLog?fpLog:stdout;
	if(fp)
	{
		char strTime[30];
		va_list va;

#if 0
		time_t t;
	    time(&t);
		strftime( strTime, sizeof(strTime), "%Y-%m-%d %H:%M:%S", localtime( &t) );
#else
		_getTime(strTime);
#endif
		fprintf(fp, "[%s] [%s] ", strTime, who);

		va_start(va, fmt);
		vfprintf(fp, fmt, va);
		va_end(va);
		fprintf(fp, "\n");
	}
}


