#ifndef __ctp_h__
#define __ctp_h__

#include "basetype.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _tagIdName {
	int id;
	const char *name;
} IDNAME;

typedef int (*SVCONVERTER)(char* str, void* val, BOOL s2v/*TRUE: string to value; FALSE: value to string*/);
#define KEYVALTYPE_INT		1	//int 
#define KEYVALTYPE_STRING	2	//char[]
#define KEYVALTYPE_POINTER	3
#define KEYVALTYPE_CALLBACK	4
#define KEYVALTYPE_HEX		5
#define KVF_KEEPVAL		0x80000000	//ORed with type
#define KVF_INITVAL		0x40000000
#define KEYTYPE(pKv) (pKv->type&0x0000FFFF)
typedef
struct _tagKeyVal {
	//const char *sKey;
	char sKey[20];
	//char *sKey;
	int type;
	void *pVal;		//pVal为指向目标值类型的指针
	union {
		int size;		//type=STRING时为pVal指向的缓冲区的大小
		int iVal;		//type=INT时, 如果pVal为空, 值保存到这里
		SVCONVERTER conv;	//type=CALLBACK, 解析操作调用conv将字符串表示转为值
	};

	char *sVal;
} KEYVAL;

#define DEFINE_STR_KEYVAL(name, var) { name, KEYVALTYPE_STRING, (void*)var, {sizeof(var)} }
#define DEFINE_INT_KEYVAL(name, var) { name, KEYVALTYPE_INT, (void*)&var }
#define DEFINE_HEX_KEYVAL(name, var) { name, KEYVALTYPE_HEX, (void*)&var }
#define DEFINE_CB_KEYVAL(name, var, cb) { name, KEYVALTYPE_CALLBACK, &var, cb }

#define CONTENTTYPE_TEXTPLAIN	0
#define CONTENTTYPE_APPOCT	1
typedef struct _tagCtpReqOpt {
	int	content_length;
	int	keep_alive;
	
	char *host;
	char *content_type;
	char *accept;
	char *accept_charset;
	char *accept_encoding;
	char *accept_language;
	char *x_sessioncookie;
	char *cookie;

	unsigned long cseq;	//CSeq
	char *session;
	char *authorization;
	char *transport;
	
	char	*body;
} REQUESTOPTIONS;

typedef struct _tagCtpRespOpt {
	
} RESPONSEOPTION;

typedef struct _tagRequestLine {
	char *method;
        char *uri;
        char *proto_ver;
        char *header;
 } REQUESTLINE;


#define PF_CASESENSITIVE	0x1
#define PF_DONTINITVALS		0x2
#define PF_ZEROTERMINATED	0x4
int ParseBody(char *pBody, KEYVAL *pKv, int cnt, DWORD dwFlags);
int ParseHttpBody(char *pBody, KEYVAL *pKv, int cnt, DWORD flags);


int GatherKeyVals(const KEYVAL* pKv, char* buffer);

extern KEYVAL *KvOf(const char *key, KEYVAL *pkv,int size);

int KV_ValueToString(const KEYVAL* pKv, char* sVal);
//May truncate ending space of s
int KV_ValueFromString(KEYVAL* pKv, char* s);

const char *CTPReasonOfErrorCode(int err);

int ParseRequestLine(char *buf, REQUESTLINE *pReqLine);
int ParseRequestOptions(char *header, REQUESTOPTIONS *pReqOpt);

int SendCtpAck(int sock, int status, const char *pBody, int len);
int DefaultSendAck(int sock, const char *pBody, int len);
void SendOKAck(int sock);
void SendChunkedAckHeader(int sock);

#ifdef __cplusplus
}
#endif

#endif
