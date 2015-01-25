#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "ctp.h"

int ParseRequestLine(char *buf, REQUESTLINE *pReqLine)
{
	char *p;

        p = strstr(buf, "\r\n");
        if(!p) return 400;

        *p = '\0';
        p += 2;
        if(strncmp(p, "\r\n", 2) == 0) *p = '\0';
        pReqLine->header = p;
 
        pReqLine->method = strtok(buf, " \t");
        pReqLine->uri = strtok(NULL, " \t");
	if(!pReqLine->uri) return 400;
        pReqLine->proto_ver = strtok(NULL, " ");
	if(!pReqLine->proto_ver) return 400;
        return 0;
}

int ParseRequestOptions(char *header, REQUESTOPTIONS *pReqOpt)
{
	char *connection = NULL;
	KEYVAL kvhead[] = {
		{ "Host", KEYVALTYPE_POINTER, &pReqOpt->host },
		{ "Content-Length", KEYVALTYPE_INT, &pReqOpt->content_length },
		{ "Accept", KEYVALTYPE_POINTER, &pReqOpt->accept },
		{ "Accept-Language", KEYVALTYPE_POINTER, &pReqOpt->accept_language },
		{ "Accept-Charset", KEYVALTYPE_POINTER, &pReqOpt->accept_charset },
		{ "Content-Type", KEYVALTYPE_POINTER, &pReqOpt->content_type },
		{ "Connection", KEYVALTYPE_POINTER, &connection },
		{ "Cookie", KEYVALTYPE_POINTER, &pReqOpt->cookie },

		{ "x-sessioncookie", KEYVALTYPE_POINTER, &pReqOpt->x_sessioncookie },

		{ "CSeq", KEYVALTYPE_INT, &pReqOpt->cseq },
		{ "Session", KEYVALTYPE_POINTER, &pReqOpt->session },
		{ "Authorization", KEYVALTYPE_POINTER, &pReqOpt->authorization },
		{ "Transport", KEYVALTYPE_POINTER, &pReqOpt->transport }
	};
	memset(pReqOpt, 0, sizeof(REQUESTOPTIONS));

	if(!*header)
	{
		pReqOpt->body = header;
		return 0;
	}

	pReqOpt->body = strstr(header, "\r\n\r\n");
	if(!pReqOpt->body) return 400;
	pReqOpt->body += 4;
	ParseBody(header, kvhead, sizeof(kvhead)/sizeof(KEYVAL), 0);
	if(connection && strcasecmp(connection, "Keep-Alive") == 0)
		pReqOpt->keep_alive = 1;
	return 0;
}


static int isSeperator(int ch)
{
	return ch>0 && (isspace(ch) || ch == ':' || ch == '=');
}

//设置无对应键或值为空时的值
void KV_SetDefaultValue(KEYVAL *pKv)
{
	pKv->sVal = NULL; 
	if(pKv->pVal)
	{
		switch(KEYTYPE(pKv))
		{
			case KEYVALTYPE_STRING: memset(pKv->pVal, 0, pKv->size); break;
			case KEYVALTYPE_INT: case KEYVALTYPE_HEX: *((int*)pKv->pVal) = 0; break;
			case KEYVALTYPE_POINTER: *((void**)pKv->pVal) = NULL; break;
			case KEYVALTYPE_CALLBACK:
						 if(pKv->conv) pKv->conv("", pKv->pVal, TRUE);
						 break;
		}
	}
}
//May truncate ending space of s
int KV_ValueFromString(KEYVAL* pKv, char* s)
{
	while(*s && (isSeperator(*s) || *s == '\"') ) s++;
	if(*s && s[strlen(s)-1]=='\"') s[strlen(s)-1] = '\0';

	pKv->sVal = s;
	switch(KEYTYPE(pKv))
	{
		case KEYVALTYPE_CALLBACK:
			if(pKv->pVal && pKv->conv) 
				pKv->conv(pKv->sVal, pKv->pVal, TRUE);
			break;
		case KEYVALTYPE_STRING:
			if(pKv->pVal)
			{
				memset(pKv->pVal, 0, pKv->size);
				strncpy((char*)pKv->pVal, s, pKv->size-1);
			}
			break;
		case KEYVALTYPE_POINTER:
			if(pKv->pVal)
			{
				*((void**)pKv->pVal) = s;
			}
			else
				pKv->pVal = s;
			break;
		case KEYVALTYPE_INT:
		case KEYVALTYPE_HEX:
			pKv->iVal = strtoul(s, NULL, 0);
			if(pKv->pVal) *(int*)(pKv->pVal) = pKv->iVal;
			break;
	}
	return 0;
}

int KV_ValueToString(const KEYVAL* pKv, char* sVal)
{
	if(pKv->pVal==NULL) return 0;
	switch (KEYTYPE(pKv))
	{
	case KEYVALTYPE_STRING:
		strcpy(sVal, (char*)(pKv->pVal));
		return strlen(sVal);
	case KEYVALTYPE_INT:
		return sprintf(sVal, "%u", *((sint32*)pKv->pVal )  );
	case KEYVALTYPE_HEX:
		return sprintf(sVal, "0x%X", *((uint32*)pKv->pVal )  );
	case KEYVALTYPE_CALLBACK:
		if(pKv->conv) return pKv->conv(sVal, pKv->pVal, FALSE);
		break;
	}
	*sVal = '\0';
	return 0;
}


//@ Parse a CTP request's body
// Maybe modify the content of pBody
int ParseBody(char *pBody, KEYVAL *pKv, int cnt, DWORD flags)
{
	int i, rval = 0;

	for(i=0; (cnt>0 && i<cnt) || (cnt<=0 && pKv[i].sKey && pKv[i].sKey[0]); i++) 
	{ 
		if(!pKv[i].sKey || !pKv[i].sKey[0]) break;
		if( ( !(flags & PF_DONTINITVALS) && !(pKv[i].type & KVF_KEEPVAL)) || (pKv[i].type & KVF_INITVAL) )
		{
			KV_SetDefaultValue(&pKv[i]);
		}
	}
	if(!pBody || *pBody == '\0') return 0;

	char *newl = pBody;
	while(1)
	{
		char *key = newl, *kend, *val, *vend;
		if(flags & PF_ZEROTERMINATED)
		{
			if(!*newl) break;
			newl += strlen(newl) + 1;
		}
		else
		{
			if( !newl || !*newl || strncmp(newl, "\r\n", 2) == 0 ) break;
			newl = strstr(newl, "\r\n");
			if(newl) { *newl = '\0'; newl += 2; }
		}

		while(isspace(*key)) key++;
		if(*key == '-') key++;
		kend = key;
		while(*kend && !isSeperator(*kend)) kend++;
		if(*kend)
		{
			val = kend + 1;
			*kend = '\0';
			while(*val && isSeperator(*val)) val++;
			if(*val)
			{
				vend = val + strlen(val);
				while(isSeperator(vend[-1])) vend--;
				if(*val == '\"' && vend[-1] == '\"')
				{
					val ++;
					vend --;;
				}
			}
			else
				vend = val;
		}
		else
		{
			val = kend;
		}

	 	//dbg_msg("%s = %s\n", key, val);
		for(i=0; (cnt>0 && i<cnt) || (cnt<=0 && pKv[i].sKey && pKv[i].sKey[0]); i++)
		{
			if(pKv[i].sKey==NULL||pKv[i].sKey[0]=='\0') break;
			const char *pKey = pKv[i].sKey;
			if(*pKey == '-') pKey++;
				
			if( ( (flags & PF_CASESENSITIVE) && strcmp(pKey, key) == 0 ) || 
					( !(flags & PF_CASESENSITIVE) && strcasecmp(pKey, key) == 0 ) )
			{
				KV_ValueFromString(&pKv[i], val );
				rval ++;

				break;
			}
				
		}
	}
	return rval;
}

///@ find the item with specified key
//parameters:
// 	pkv - An array of KEYVAL which's size is specified (size > 0), or 
// 		(size<=0) end with an empty KEYVAL item.
// 		an empty item's sKey is NULL or strlen(sKey) is 0.
KEYVAL *KvOf(const char *key, KEYVAL *pkv, int size)
{
	int i=0;

	for(i=0; (size>0 && i<size) || (size<=0&&pkv->sKey && pkv->sKey[0]); i++)
	{
		if(strcmp(key, pkv->sKey[0]=='-'?(pkv->sKey+1):pkv->sKey) == 0) 
		{
			return (KEYVAL *)pkv;
		}
		pkv++;
	}
	return NULL;
}

static int valOf(char c)
{
	if(isdigit(c)) return c - '0';
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	return 0;
}

int FormatHttpString(const char *s, char *t)
{
	if(!s || !t) return 0;
	char *t0 = t;

	while(*s)
	{
		if(*s == '%')
		{
			char c;
			s++;
			c = valOf(*s++) << 4;
			c |= valOf(*s++);
			*t++ = c;
		}
		else
			*t++ = *s++;
	}
	*t = '\0';
	return t - t0;
}

//May modify the content of pBody
int ParseHttpBody(char *pBody, KEYVAL *pKv, int cnt, DWORD flags)
{
	int i, rval = 0;
	char *name;

	for(i=0; (cnt>0&&i<cnt) || (cnt<=0&&pKv[i].sKey&&pKv[i].sKey[0]); i++) 
	{ 
		if(!pKv[i].sKey || !pKv[i].sKey[0]) break;
		if( ( !(flags & PF_DONTINITVALS) && !(pKv[i].type & KVF_KEEPVAL) ) || (pKv[i].type & KVF_INITVAL) )
		{
			KV_SetDefaultValue(&pKv[i]);
		}
	}

	if(!pBody) return 0;
	name = strtok(pBody, "&\r\n");
	while(name)
	{
		char *val, *vend;
		if( !(val = strchr(name, '=')) ) 
		{
			name = strtok(NULL, "&\r\n");
			continue;
		}

		*val++ = '\0';
		vend = val;
		while(*vend) vend++;
		for(i=0; (cnt>0&&i<cnt)||(cnt<=0&&pKv[i].sKey&&pKv[i].sKey[0]); i++)
		{
			if(pKv[i].sKey==NULL || pKv[i].sKey[0]=='\0') break;
			//dbg_msg("%s: %dth key = %s\n", name, i, pKv[i].sKey);
			const char *pKey = pKv[i].sKey;
			if( ( (flags & PF_CASESENSITIVE) && strcmp(pKey, name) == 0 ) || 
				( !(flags & PF_CASESENSITIVE) && strcasecmp(pKey, name) == 0 ) )
			{
				//dbg_msg("val = %s\n", val);
				FormatHttpString(val, val);
				KV_ValueFromString(&pKv[i], val);
				rval ++;
				break;
			}
		}
		name = strtok(NULL, "&\r\n");
	}
	return rval;
}

int GatherKeyVals(const KEYVAL* pKv, char* buffer)
{
	int len = 0;
	while(pKv && pKv->sKey && pKv->sKey[0])
	{
		switch(KEYTYPE(pKv))
		{
		case KEYVALTYPE_HEX:
			len += sprintf(buffer+len, "-%s 0x%X\r\n", pKv->sKey, *(unsigned int*)(pKv->pVal));
			break;
		case KEYVALTYPE_INT:
			len += sprintf(buffer+len, "-%s %d\r\n", pKv->sKey, *(int*)(pKv->pVal));
			break;
		case KEYVALTYPE_STRING:
			len += sprintf(buffer+len, "-%s %s\r\n", pKv->sKey, (char*)pKv->pVal);
			break;
		case KEYVALTYPE_POINTER:
			len += sprintf(buffer+len, "-%s %s\r\n", pKv->sKey, *((char**)pKv->pVal));
			break;
		case KEYVALTYPE_CALLBACK:
			if(pKv->conv)
			{
				len += sprintf(buffer+len, "-%s ", pKv->sKey);
				len += pKv->conv(buffer+len, pKv->pVal, FALSE);
				strcpy(buffer+len, "\r\n");
				len += 2;
			}
			break;
		}
		pKv++;
	}
	if(len) 
	{
		strcpy(buffer+len, "\r\n");
		len += 2;
	}
	else
		*buffer = '\0';
	return len;
}

int SendCtpAck(int sock, int status, const char *pBody, int len)
{
	char head[200];
	struct iovec v[2];
	int hdrLen;

	if(status == 0) status = 200;
	if(!pBody) len = 0;
	else if(len < 0) len = strlen(pBody);

	hdrLen = sprintf(head, "CTP/1.0 %d %s\r\nContent-Length: %d\r\n\r\n", status, CTPReasonOfErrorCode(status), len);

	v[0].iov_base = head;
	v[0].iov_len = hdrLen;
	v[1].iov_base = pBody;
	v[1].iov_len = len;

	writev(sock, v, 2);
	return 0;

}

int DefaultSendAck(int sock, const char *pBody, int len)
{
	char head[200];
	struct iovec v[2];
	
	v[1].iov_base =(void *)pBody;
	v[1].iov_len = len;
	v[0].iov_base = head;
	v[0].iov_len = sprintf(head, "CTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n", v[1].iov_len);
	writev(sock, v, 2);

	return 0;
}

void SendOKAck(int sock)
{
	const char ack[] = "CTP/1.0 200 OK\r\n\r\n";
	send(sock, (char*)ack, sizeof(ack)-1, 0);
}

void SendChunkedAckHeader(int sock)
{
	const char chunk_hdr[] = "CTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
       send(sock, chunk_hdr, sizeof(chunk_hdr)-1, 0);	
}


////////////////////////////////////////////////////////////////////////////////////////////////

IDNAME ctperrors[] = {
	{ 100, "Continue" },
	{ 101, "Switching Protocols" },
	{ 200, "OK" },
	{ 201, "Created" },
	{ 202, "Accepted" },
	{ 203, "Non-Authoritative Information" },
	{ 204, "No Content" },
	{ 205, "Reset Content" },
	{ 206, "Partial Content" },
	{ 300, "Multiple Choices" },
	{ 301, "Moved Permanently" },
	{ 302, "Found" },
	{ 303, "See Other" },
	{ 304, "Not Modified" },
	{ 305, "Use Proxy" },
	{ 307, "Temporary Redirect" },
	{ 400, "Bad Request" },
	{ 401, "Unauthorized" },
	{ 402, "Payment Required" },
	{ 403, "Forbidden" },
	{ 404, "Not Found" },
	{ 405, "Method Not Allowed" },
	{ 406, "Not Acceptable" },
	{ 407, "Proxy Authentication Required" },
	{ 408, "Request Time-out" },
	{ 409, "Confilict" },
	{ 410, "Gone" },
	{ 411, "Length Required" },
	{ 412, "Precondition Failed" },
	{ 413, "Request Entity Too Large" },
	{ 414, "Request-URI Too Large" },
	{ 415, "Unsupported Media Type" },
	{ 416, "Requested range not satisfiable" },
	{ 417, "ExpectationFailed" },
	{ 451, "Parameter Not Understood" },
	{ 452, "Conference Not Found" },
	{ 454, "Session Not Found" },

		
	{ 500, "Internal Server Error" },
	{ 501, "Service Unavailable" },
	{ 502, "Bad Gateway" },
	{ 503, "Service Unavailable" },
	{ 504, "Gateway Time-out" },
	{ 505, "HTTP Version not supported" },

	{ 901, "General Error" },

	{ 0, "" }
};

const char *CTPReasonOfErrorCode(int err)
{
	IDNAME *p = &ctperrors[0];
	while(p->id)
	{
		if(p->id == err) return p->name;
		p++;
	}
	return p->name;
}
	
