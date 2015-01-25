#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>

#include "platform_adpt.h"
#include "rtsp_parse.h"
#include "ctp.h"


typedef struct {
	char *token;
	int opcode;
} RTSP_TKN_S;

RTSP_TKN_S RTSP_Status [] =
{
   {"Continue", 100},
   {"OK", 200},
   {"Created", 201},
   {"Accepted", 202},
   {"Non-Authoritative Information", 203},
   {"No Content", 204},
   {"Reset Content", 205},
   {"Partial Content", 206},
   {"Multiple Choices", 300},
   {"Moved Permanently", 301},
   {"Moved Temporarily", 302},
   {"Bad Request", 400},
   {"Unauthorized", 401},
   {"Payment Required", 402},
   {"Forbidden", 403},
   {"Not Found", 404},
   {"Method Not Allowed", 405},
   {"Not Acceptable", 406},
   {"Proxy Authentication Required", 407},
   {"Request Time-out", 408},
   {"Conflict", 409},
   {"Gone", 410},
   {"Length Required", 411},
   {"Precondition Failed", 412},
   {"Request Entity Too Large", 413},
   {"Request-URI Too Large", 414},
   {"Unsupported Media Type", 415},
   {"Bad Extension", 420},
   {"Invalid Parameter", 450},
   {"Parameter Not Understood", 451},
   {"Conference Not Found", 452},
   {"Not Enough Bandwidth", 453},
   {"Session Not Found", 454},
   {"Method Not Valid In This State", 455},
   {"Header Field Not Valid for Resource", 456},
   {"Invalid Range", 457},
   {"Parameter Is Read-Only", 458},
   {"Internal Server Error", 500},
   {"Not Implemented", 501},
   {"Bad Gateway", 502},
   {"Service Unavailable", 503},
   {"Gateway Time-out", 504},
   {"RTSP Version Not Supported", 505},
   {"Extended Error:", 911},
   {0, HI_RTSP_PARSE_INVALID_OPCODE}
};

char  *RTSP_Invalid_Method_STR = "Invalid Method";

/* message header keywords */

char *RTSP_Get_Status_Str( int code )
{
   RTSP_TKN_S  *pRtspTkn;
   
   for ( pRtspTkn = RTSP_Status; pRtspTkn->opcode != HI_RTSP_PARSE_INVALID_OPCODE; pRtspTkn++ )
   {
      if ( pRtspTkn->opcode == code )
      {
         return( pRtspTkn->token );
      }
   }

   return( RTSP_Invalid_Method_STR );
}

/*porting from oms project: parse_url */
BOOL RTSP_Parse_Url(const char *url, char *server, int *port, char *file_name, char *user, char *password)
{
	/* expects format '[rtsp://[user:password@]server[:port]/]filename[?param=encoded_parameters]' */
	/* parameters: username=xxx&password=yyyy&expire=nnnn */

	BOOL ret = FALSE;
	/* copy url */
	*port = RTSP_DEFAULT_SVR_PORT;
	if(user) *user = '\0';
	if(password) *password = '\0';
	if (strncmp(url, "rtsp://", 7) == 0) 
	{
		char *param;
		char *p1 =(char*)(url+7);
		char *p2 = p1;
		int hasAccount = strchr(url, '@');

		param = strchr(p1, '?');
		if(param) *param = '\0';	//set to '\0' temporarily for convenionce of string operation
		while(*p2 && *p2 != '/')
		{
			if(*p2 == '@')
			{
				if(password) strncpyz(password, p1, p2-p1+1);
				p1 = p2 + 1;
				hasAccount = 0;
			}
			else if(*p2 == ':')
			{
				if(hasAccount)
				{
					if(user) strncpyz(user, p1, p2-p1+1);
					p1 = p2 + 1;
				}
				else
					break;
			}
			p2++;
		}
		strncpy(server, p1, p2-p1);
		server[p2-p1] = '\0';

		if(*p2 == ':') {
			*port = atoi(p2+1);
			while(*p2 && *p2 != '/') p2++;
		}
		strcpy(file_name, p2+1);

		if(param)
		{
			*param++ = '?';	//restore
			if(strncmp("param=", param, 6) == 0)
			{
				char str[128], *usr, *pswd;

				KEYVAL kv[] = {
					{ "username", KEYVALTYPE_POINTER, &usr },
					{ "password", KEYVALTYPE_POINTER, &pswd }
				};
				memset(str, 0, sizeof(str));
				lutil_b64_pton(param+6, str, sizeof(str));
				ParseHttpBody(str, kv, sizeof(kv)/sizeof(KEYVAL), 0);
				if(usr && user) strcpy(user, usr);
				if(pswd && password) strcpy(password, pswd);
			}
		}
		dbg_msg("Parse rtsp usl: %s\nResult: user=%s,password=%s, host=%s, port=%d, path=%s\n", 
						url, user?user:"", password?password:"", server, *port, file_name);

		return TRUE;
	} 
	else 
	{
		/* try just to extract a file name */
		char *token = strtok((char*)url, " \t\n");
		if (token) 
		{
			strcpy(file_name, token);
			server[0] = '\0';
			ret = 0;
		}
	}

	return ret;
}

