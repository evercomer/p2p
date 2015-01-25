#if defined(__sparc__) || defined(WIN32)
#define NOSSL
#endif
#define NOSSL

#include "udp.h"
#include "stun.h"
#ifdef __LINUX__
#include <linux/if.h>
#include <sys/ioctl.h>
#endif
#include <assert.h>

#ifdef __LINUX__
#define dbg_stun(fmt, args...) 
#elif defined(WIN32)
#define dbg_stun(fmt, __VA_ARGS__)
#endif


static void computeHmac(char* hmac, const char* input, int length, const char* key, int keySize);

static BOOL stunParseAtrAddress( char* body, unsigned int hdrLen,  StunAtrAddress4* result )
{
	if ( hdrLen != 8 )
	{
		dbg_stun("hdrLen wrong for Address\n");
		return FALSE;
	}
	result->pad = *body++;
	result->family = *body++;
	if (result->family == IPv4Family)
	{
		UInt16 nport;
		UInt32 naddr;

		memcpy(&nport, body, 2); body+=2;
		result->ipv4.port = ntohs(nport);

		memcpy(&naddr, body, 4); body+=4;
		result->ipv4.addr = ntohl(naddr);
		return TRUE;
	}
	else if (result->family == IPv6Family)
	{
		dbg_stun("ipv6 not supported\n");
	}
	else
	{
		dbg_stun("bad address family: %d", result->family);
	}

	return FALSE;
}

static BOOL stunParseAtrChangeRequest( char* body, unsigned int hdrLen,  StunAtrChangeRequest* result )
{
	if ( hdrLen != 4 )
	{
		dbg_stun("hdr length = %d expecting %d\n", hdrLen, sizeof(result));
		dbg_stun("Incorrect size for ChangeRequest\n");
		return FALSE;
	}
	else
	{
		memcpy(&result->value, body, 4);
		result->value = ntohl(result->value);
		return TRUE;
	}
}

static BOOL stunParseAtrError( char* body, unsigned int hdrLen,  StunAtrError* result )
{
	if ( hdrLen >= sizeof(result) )
	{
		dbg_stun("head on Error too large\n");
		return FALSE;
	}
	else
	{
		memcpy(&result->pad, body, 2); body+=2;
		result->pad = ntohs(result->pad);
		result->errorClass = *body++;
		result->number = *body++;

		result->sizeReason = hdrLen - 4;
		memcpy(&result->reason, body, result->sizeReason);
		result->reason[result->sizeReason] = 0;
		return TRUE;
	}
}

static BOOL stunParseAtrUnknown( char* body, unsigned int hdrLen,  StunAtrUnknown* result )
{
	if ( hdrLen >= sizeof(result) )
	{
		return FALSE;
	}
	else
	{
		int i;

		if (hdrLen % 4 != 0) return FALSE;
		result->numAttributes = hdrLen / 4;
		for (i=0; i<result->numAttributes; i++)
		{
			memcpy(&result->attrType[i], body, 2); body+=2;
			result->attrType[i] = ntohs(result->attrType[i]);
		}
		return TRUE;
	}
}


static BOOL stunParseAtrString( char* body, unsigned int hdrLen,  StunAtrString* result )
{
	if ( hdrLen >= STUN_MAX_STRING )
	{
		dbg_stun("String is too large\n");
		return FALSE;
	}
	else
	{
		if (hdrLen % 4 != 0)
		{
			dbg_stun("Bad length string %d\n", hdrLen);
			return FALSE;
		}

		result->sizeValue = hdrLen;
		memcpy(&result->value, body, hdrLen);
		result->value[hdrLen] = 0;
		return TRUE;
	}
}


static BOOL stunParseAtrIntegrity( char* body, unsigned int hdrLen,  StunAtrIntegrity* result )
{
	if ( hdrLen != 20)
	{
		dbg_stun("MessageIntegrity must be 20 bytes\n");
		return FALSE;
	}
	else
	{
		memcpy(&result->hash, body, hdrLen);
		return TRUE;
	}
}


BOOL stunParseMessage( char* buf, unsigned int bufLen, StunMessage* msg)
{
	char* body;
	unsigned int size;

	dbg_stun("Received stun message: %d bytes\n", bufLen);
	memset(msg, 0, sizeof(*msg));

	if (sizeof(StunMsgHdr) > bufLen)
	{
		dbg_stun("Bad message\n");
		return FALSE;
	}

	memcpy(&msg->msgHdr, buf, sizeof(StunMsgHdr));
	msg->msgHdr.msgType = ntohs(msg->msgHdr.msgType);
	msg->msgHdr.msgLength = ntohs(msg->msgHdr.msgLength);

	if (msg->msgHdr.msgLength + sizeof(StunMsgHdr) != bufLen)
	{
		dbg_stun("Message header length doesn't match message size: %d - %d\n",
			msg->msgHdr.msgLength, bufLen);
		return FALSE;
	}

	body = buf + sizeof(StunMsgHdr);
	size = msg->msgHdr.msgLength;

	//dbg_stun("bytes after header = " << size << );

	while ( size > 0 )
	{
		// !jf! should check that there are enough bytes left in the buffer

		StunAtrHdr* attr = (StunAtrHdr*)(body);

		unsigned int attrLen = ntohs(attr->length);
		int atrType = ntohs(attr->type);

		if ( attrLen+4 > size ) 
		{
			dbg_stun("claims attribute is larger than size of message (attribute type=%d)\n", atrType);
			return FALSE;
		}

		body += 4; // skip the length and type in attribute header
		size -= 4;

		switch ( atrType )
		{
			case MappedAddress:
				msg->hasMappedAddress = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen,  &msg->mappedAddress )== FALSE )
				{
					dbg_stun("problem parsing MappedAddress\n");
					return FALSE;
				}
				else
				{
					dbg_stun("MappedAddress = %s\n",  stunAddress4ToString(&msg->mappedAddress.ipv4));
				}

				break;  

			case ResponseAddress:
				msg->hasResponseAddress = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen,  &msg->responseAddress )== FALSE )
				{
					dbg_stun("problem parsing ResponseAddress\n");
					return FALSE;
				}
				else
				{
					dbg_stun("ResponseAddress = %s\n", stunAddress4ToString(&msg->responseAddress.ipv4));
				}
				break;  

			case ChangeRequest:
				msg->hasChangeRequest = TRUE;
				if (stunParseAtrChangeRequest( body, attrLen, &msg->changeRequest) == FALSE)
				{
					dbg_stun("problem parsing ChangeRequest\n");
					return FALSE;
				}
				else
				{
					dbg_stun("ChangeRequest = %u\n", msg->changeRequest.value);
				}
				break;

			case SourceAddress:
				msg->hasSourceAddress = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen,  &msg->sourceAddress )== FALSE )
				{
					dbg_stun("problem parsing SourceAddress\n");
					return FALSE;
				}
				else
				{
					dbg_stun("SourceAddress = %s\n", stunAddress4ToString(&msg->sourceAddress.ipv4));
				}
				break;  

			case ChangedAddress:
				msg->hasChangedAddress = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen,  &msg->changedAddress )== FALSE )
				{
					dbg_stun("problem parsing ChangedAddress\n");
					return FALSE;
				}
				else
				{
					dbg_stun("ChangedAddress = %s\n", stunAddress4ToString(&msg->changedAddress.ipv4));
				}
				break;  

			case Username: 
				msg->hasUsername = TRUE;
				if (stunParseAtrString( body, attrLen, &msg->username) == FALSE)
				{
					dbg_stun("problem parsing Username\n");
					return FALSE;
				}
				else
				{
					dbg_stun("Username = %s\n", msg->username.value);
				}

				break;

			case Password: 
				msg->hasPassword = TRUE;
				if (stunParseAtrString( body, attrLen, &msg->password) == FALSE)
				{
					dbg_stun("problem parsing Password\n");
					return FALSE;
				}
				else
				{
					dbg_stun("Password = %s\n", msg->password.value);
				}
				break;

			case MessageIntegrity:
				msg->hasMessageIntegrity = TRUE;
				if (stunParseAtrIntegrity( body, attrLen, &msg->messageIntegrity) == FALSE)
				{
					dbg_stun("problem parsing MessageIntegrity\n");
					return FALSE;
				}
				else
				{
					//dbg_stun("MessageIntegrity = %d\n" << msg->messageIntegrity.hash);
				}

				// read the current HMAC
				// look up the password given the user of given the transaction id 
				// compute the HMAC on the buffer
				// decide if they match or not
				break;

			case ErrorCode:
				msg->hasErrorCode = TRUE;
				if (stunParseAtrError(body, attrLen, &msg->errorCode) == FALSE)
				{
					dbg_stun("problem parsing ErrorCode\n");
					return FALSE;
				}
				else
				{
					dbg_stun("ErrorCode = %d  %d  %s\n", (int)(msg->errorCode.errorClass),
						(int)(msg->errorCode.number), msg->errorCode.reason);
				}

				break;

			case UnknownAttribute:
				msg->hasUnknownAttributes = TRUE;
				if (stunParseAtrUnknown(body, attrLen, &msg->unknownAttributes) == FALSE)
				{
					dbg_stun("problem parsing UnknownAttribute\n");
					return FALSE;
				}
				break;

			case ReflectedFrom:
				msg->hasReflectedFrom = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen, &msg->reflectedFrom ) == FALSE )
				{
					dbg_stun("problem parsing ReflectedFrom\n");
					return FALSE;
				}
				break;  

			case XorMappedAddress:
				msg->hasXorMappedAddress = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen,  &msg->xorMappedAddress ) == FALSE )
				{
					dbg_stun("problem parsing XorMappedAddress\n");
					return FALSE;
				}
				else
				{
					dbg_stun("XorMappedAddress = %s\n", stunAddress4ToString(&msg->mappedAddress.ipv4));
				}
				break;  

			case XorOnly:
				msg->xorOnly = TRUE;
				dbg_stun("xorOnly = TRUE\n");
				break;  

			case ServerName: 
				msg->hasServerName = TRUE;
				if (stunParseAtrString( body, attrLen, &msg->serverName) == FALSE)
				{
					dbg_stun("problem parsing ServerName\n");
					return FALSE;
				}
				else
				{
					dbg_stun("ServerName = %s\n", msg->serverName.value);
				}
				break;

			case SecondaryAddress:
				msg->hasSecondaryAddress = TRUE;
				if ( stunParseAtrAddress(  body,  attrLen,  &msg->secondaryAddress ) == FALSE )
				{
					dbg_stun("problem parsing secondaryAddress\n");
					return FALSE;
				}
				else
				{
					dbg_stun("SecondaryAddress = %s\n", stunAddress4ToString(&msg->secondaryAddress.ipv4));
				}
				break;  

			default:
				dbg_stun("Unknown attribute: %d\n", atrType);
				if ( atrType <= 0x7FFF ) 
				{
					return FALSE;
				}
		}

		body += attrLen;
		size -= attrLen;
	}

	return TRUE;
}


static char* encode16(char* buf, UInt16 data)
{
   UInt16 ndata = htons(data);
   memcpy(buf, &ndata, sizeof(UInt16));
   return buf + sizeof(UInt16);
}

static char* encode32(char* buf, UInt32 data)
{
   UInt32 ndata = htonl(data);
   memcpy(buf, &ndata, sizeof(UInt32));
   return buf + sizeof(UInt32);
}


static char* encode(char* buf, const char* data, unsigned int length)
{
   memcpy(buf, data, length);
   return buf + length;
}


static char* encodeAtrAddress4(char* ptr, UInt16 type, const StunAtrAddress4* atr)
{
   ptr = encode16(ptr, type);
   ptr = encode16(ptr, 8);
   *ptr++ = atr->pad;
   *ptr++ = IPv4Family;
   ptr = encode16(ptr, atr->ipv4.port);
   ptr = encode32(ptr, atr->ipv4.addr);
	
   return ptr;
}

static char* encodeAtrChangeRequest(char* ptr, const StunAtrChangeRequest* atr)
{
   ptr = encode16(ptr, ChangeRequest);
   ptr = encode16(ptr, 4);
   ptr = encode32(ptr, atr->value);
   return ptr;
}

static char* encodeAtrError(char* ptr, const StunAtrError* atr)
{
   ptr = encode16(ptr, ErrorCode);
   ptr = encode16(ptr, 6 + atr->sizeReason);
   ptr = encode16(ptr, atr->pad);
   *ptr++ = atr->errorClass;
   *ptr++ = atr->number;
   ptr = encode(ptr, atr->reason, atr->sizeReason);
   return ptr;
}

static char* encodeAtrUnknown(char* ptr, const StunAtrUnknown* atr)
{
   int i;
   ptr = encode16(ptr, UnknownAttribute);
   ptr = encode16(ptr, 2+2*atr->numAttributes);
   for (i=0; i<atr->numAttributes; i++)
   {
      ptr = encode16(ptr, atr->attrType[i]);
   }
   return ptr;
}

static char* encodeXorOnly(char* ptr)
{
   ptr = encode16(ptr, XorOnly );
   return ptr;
}


static char* encodeAtrString(char* ptr, UInt16 type, const StunAtrString* atr)
{
   assert(atr->sizeValue % 4 == 0);
	
   ptr = encode16(ptr, type);
   ptr = encode16(ptr, atr->sizeValue);
   ptr = encode(ptr, atr->value, atr->sizeValue);
   return ptr;
}

static char* encodeAtrIntegrity(char* ptr, const StunAtrIntegrity* atr)
{
   ptr = encode16(ptr, MessageIntegrity);
   ptr = encode16(ptr, 20);
   ptr = encode(ptr, atr->hash, sizeof(atr->hash));
   return ptr;
}

unsigned int stunEncodeMessage( const StunMessage* msg, 
                   char* buf, 
                   unsigned int bufLen, 
                   const StunAtrString* password
                   )
{
	char *ptr, *lengthp;

   assert(bufLen >= sizeof(StunMsgHdr));
	
   ptr = encode16(buf, msg->msgHdr.msgType);
   lengthp = ptr;
   ptr = encode16(ptr, 0);
   ptr = encode(ptr, (const char*)(msg->msgHdr.id.octet), sizeof(msg->msgHdr.id));
	
   dbg_stun("Encoding stun message: \n");
   if (msg->hasMappedAddress)
   {
      dbg_stun("Encoding MappedAddress: %s\n", stunAddress4ToString(&msg->mappedAddress.ipv4));
      ptr = encodeAtrAddress4 (ptr, MappedAddress, &msg->mappedAddress);
   }
   if (msg->hasResponseAddress)
   {
      dbg_stun("Encoding ResponseAddress: %s\n", stunAddress4ToString(&msg->responseAddress.ipv4));
      ptr = encodeAtrAddress4(ptr, ResponseAddress, &msg->responseAddress);
   }
   if (msg->hasChangeRequest)
   {
      dbg_stun("Encoding ChangeRequest: %u\n", msg->changeRequest.value);
      ptr = encodeAtrChangeRequest(ptr, &msg->changeRequest);
   }
   if (msg->hasSourceAddress)
   {
      dbg_stun("Encoding SourceAddress: %s\n", stunAddress4ToString(&msg->sourceAddress.ipv4));
      ptr = encodeAtrAddress4(ptr, SourceAddress, &msg->sourceAddress);
   }
   if (msg->hasChangedAddress)
   {
      dbg_stun("Encoding ChangedAddress: %s\n", stunAddress4ToString(&msg->changedAddress.ipv4));
      ptr = encodeAtrAddress4(ptr, ChangedAddress, &msg->changedAddress);
   }
   if (msg->hasUsername)
   {
      dbg_stun("Encoding Username: %s\n", msg->username.value);
      ptr = encodeAtrString(ptr, Username, &msg->username);
   }
   if (msg->hasPassword)
   {
      dbg_stun("Encoding Password: %s\n", msg->password.value);
      ptr = encodeAtrString(ptr, Password, &msg->password);
   }
   if (msg->hasErrorCode)
   {
      dbg_stun("Encoding ErrorCode: class=%d number=%d reason=%s\n", 
			(int)(msg->errorCode.errorClass), 
			(int)(msg->errorCode.number),
			msg->errorCode.reason 
			);
		
      ptr = encodeAtrError(ptr, &msg->errorCode);
   }
   if (msg->hasUnknownAttributes)
   {
      dbg_stun("Encoding UnknownAttribute: ???\n");
      ptr = encodeAtrUnknown(ptr, &msg->unknownAttributes);
   }
   if (msg->hasReflectedFrom)
   {
      dbg_stun("Encoding ReflectedFrom: %s\n", stunAddress4ToString(&msg->reflectedFrom.ipv4));
      ptr = encodeAtrAddress4(ptr, ReflectedFrom, &msg->reflectedFrom);
   }
   if (msg->hasXorMappedAddress)
   {
      dbg_stun("Encoding XorMappedAddress: %s\n", stunAddress4ToString(&msg->xorMappedAddress.ipv4));
      ptr = encodeAtrAddress4 (ptr, XorMappedAddress, &msg->xorMappedAddress);
   }
   if (msg->xorOnly)
   {
      dbg_stun("Encoding xorOnly: \n");
      ptr = encodeXorOnly( ptr );
   }
   if (msg->hasServerName)
   {
      dbg_stun("Encoding ServerName: %s\n", msg->serverName.value);
      ptr = encodeAtrString(ptr, ServerName, &msg->serverName);
   }
   if (msg->hasSecondaryAddress)
   {
      dbg_stun("Encoding SecondaryAddress: %s\n", stunAddress4ToString(&msg->secondaryAddress.ipv4));
      ptr = encodeAtrAddress4 (ptr, SecondaryAddress, &msg->secondaryAddress);
   }

   if (password->sizeValue > 0)
   {
      StunAtrIntegrity integrity;

	  dbg_stun("HMAC with password: %s\n", password->value);		
      computeHmac(integrity.hash, buf, (int)(ptr-buf) , password->value, password->sizeValue);
      ptr = encodeAtrIntegrity(ptr, &integrity);
   }
	
   encode16(lengthp, (UInt16)(ptr - buf - sizeof(StunMsgHdr)));
   return (int)(ptr - buf);
}

int stunRand()
{
   static BOOL init=FALSE;
   // return 32 bits of random stuff
   assert( sizeof(int) == 4 );
   if ( !init )
   { 
      UInt64 tick;
	  int seed;

#if defined(WIN32) 
      volatile unsigned int lowtick=0,hightick=0;
      __asm
         {
            rdtsc 
               mov lowtick, eax
               mov hightick, edx
               }
      tick = hightick;
      tick <<= 32;
      tick |= lowtick;
#elif defined(__GNUC__) && ( defined(__i686__) || defined(__i386__) )
      asm("rdtsc" : "=A" (tick));
#elif defined (__SUNPRO_CC) || defined( __sparc__ )	
      tick = gethrtime();
#elif defined(__MACH__) 
      int fd=open("/dev/random",O_RDONLY);
      read(fd,&tick,sizeof(tick));
      PA_SocketClose(fd);
#else
      tick = time(NULL);
#endif 
      seed = (int)(tick);
#ifdef WIN32
      srand(seed);
#else
      srandom(seed);
#endif
	  init = TRUE;		
   }
	
#ifdef WIN32
   {
   assert( RAND_MAX == 0x7fff );
   int r1 = rand();
   int r2 = rand();
	
   int ret = (r1<<16) + r2;
	
   return ret;
   }
#else
   return random(); 
#endif
}


/// return a random number to use as a port 
int
stunRandomPort()
{
   int min=0x4000;
   int max=0x7FFF;
	
   int ret = stunRand();
   ret = ret|min;
   ret = ret&max;
	
   return ret;
}


#ifdef NOSSL
static void
computeHmac(char* hmac, const char* input, int length, const char* key, int sizeKey)
{
   strncpy(hmac,"hmac-not-implemented",20);
}
#else
#include <openssl/hmac.h>

static void
computeHmac(char* hmac, const char* input, int length, const char* key, int sizeKey)
{
   unsigned int resultSize=0;
   HMAC(EVP_sha1(), 
        key, sizeKey, 
        reinterpret_cast<const unsigned char*>(input), length, 
        reinterpret_cast<unsigned char*>(hmac), &resultSize);
   assert(resultSize == 20);
}
#endif


static void
toHex(const char* buffer, int bufferSize, char* output) 
{
   static char hexmap[] = "0123456789abcdef";
	
   const char* p = buffer;
   char* r = output;
   int i;
   for (i=0; i < bufferSize; i++)
   {
      unsigned char temp = *p++;
		
      int hi = (temp & 0xf0)>>4;
      int low = (temp & 0xf);
		
      *r++ = hexmap[hi];
      *r++ = hexmap[low];
   }
   *r = 0;
}

void
stunCreateUserName(const StunAddress4* source, StunAtrString* username)
{
   UInt64 time = stunGetSystemTimeSecs();
   time -= (time % 20*60);
   //UInt64 hitime = time >> 32;
   UInt64 lotime = time & 0xFFFFFFFF;
	
   char buffer[1024];
   sprintf(buffer,
           "%08x:%08x:%08x:", 
           (UInt32)(source->addr),
           (UInt32)(stunRand()),
           (UInt32)(lotime));
   assert( strlen(buffer) < 1024 );
	
   assert(strlen(buffer) + 41 < STUN_MAX_STRING);
	
   char hmac[20];
   char key[] = "Jason";
   computeHmac(hmac, buffer, strlen(buffer), key, strlen(key) );
   char hmacHex[41];
   toHex(hmac, 20, hmacHex );
   hmacHex[40] =0;
	
   strcat(buffer,hmacHex);
	
   int l = strlen(buffer);
   assert( l+1 < STUN_MAX_STRING );
   assert( l%4 == 0 );
   
   username->sizeValue = l;
   memcpy(username->value,buffer,l);
   username->value[l]=0;
	
}

void
stunCreatePassword(const StunAtrString* username, StunAtrString* password)
{
   char hmac[20];
   char key[] = "Fluffy";
   //char buffer[STUN_MAX_STRING];
   computeHmac(hmac, username->value, strlen(username->value), key, strlen(key));
   toHex(hmac, 20, password->value);
   password->sizeValue = 40;
   password->value[40]=0;
	
   //dbg_stun("password=" << password->value << );
}


UInt64
stunGetSystemTimeSecs()
{
	UInt64 time=0;
#if defined(WIN32)  
	SYSTEMTIME t;
	// CJ TODO - this probably has bug on wrap around every 24 hours
	GetSystemTime( &t );
	time = (t.wHour*60+t.wMinute)*60+t.wSecond; 
#else
	struct timeval now;
	gettimeofday( &now , NULL );
	//assert( now );
	time = now.tv_sec;
#endif
	return time;
}


void outputUInt128(const UInt128 *r )
{
	static char s[64];
	int len, i;
	len = sprintf(s, "%x", (int)(r->octet[0]));
	for ( i=1; i<16; i++ )
	{
		len += sprintf(s+len, ":%x", (int)(r->octet[i]));
	}
}

char *stunAddress4ToString(const StunAddress4* addr)
{
	static char s[24];
	UInt32 ip = addr->addr;
	sprintf(s, "%d.%d.%d.%d:%d", ((int)(ip>>24)&0xFF), ((int)(ip>>16)&0xFF), ((int)(ip>> 8)&0xFF), ((int)(ip>> 0)&0xFF), addr->port);
	return s;
}


// returns TRUE if it scucceeded
BOOL 
stunParseHostName( const char* peerName,
               UInt32*ip,
               UInt16* portVal,
               UInt16 defaultPort )
{
	struct in_addr sin_addr;

	char host[512];
	strncpy(host,peerName,512);
	host[512-1]='\0';
	char* port = NULL;

	int portNum = defaultPort;

	// pull out the port part if present.
	char* sep = strchr(host,':');

	if ( sep == NULL )
	{
		portNum = defaultPort;
	}
	else
	{
		*sep = '\0';
		port = sep + 1;
		// set port part

		char* endPtr=NULL;

		portNum = strtol(port,&endPtr,10);

		if ( endPtr != NULL )
		{
			if ( *endPtr != '\0' )
			{
				portNum = defaultPort;
			}
		}
	}

	if ( portNum < 1024 ) return FALSE;
	if ( portNum >= 0xFFFF ) return FALSE;

	// figure out the host part 
	struct hostent* h;

#ifdef WIN32
	assert( strlen(host) >= 1 );
	if ( isdigit( host[0] ) )
	{
		// assume it is a ip address 
		unsigned long a = inet_addr(host);
		//cerr << "a=0x" << hex << a << dec << );

		*ip = ntohl( a );
	}
	else
	{
		// assume it is a host name 
		h = gethostbyname( host );

		if ( h == NULL )
		{
			int err = PA_SocketGetError();
			dbg_stun("error was %d\n", err);
			assert( err != WSANOTINITIALISED );

			*ip = ntohl( 0x7F000001L );

			return FALSE;
		}
		else
		{
			sin_addr = *(struct in_addr*)h->h_addr;
			*ip = ntohl( sin_addr.s_addr );
		}
	}

#else
	h = gethostbyname( host );
	if ( h == NULL )
	{
		int err = PA_SocketGetError();
		fprintf(stderr, "error was %d\n", err);
		*ip = ntohl( 0x7F000001L );
		return FALSE;
	}
	else
	{
		sin_addr = *(struct in_addr*)h->h_addr;
		*ip = ntohl( sin_addr.s_addr );
	}
#endif

	*portVal = portNum;

	return TRUE;
}


BOOL
stunParseServerName( const char* name, StunAddress4* addr)
{
   assert(name);
	
   // TODO - put in DNS SRV stuff.
	
   BOOL ret = stunParseHostName( name, &addr->addr, &addr->port, 3478); 
   if ( ret != TRUE ) 
   {
       addr->port=0xFFFF;
   }	
   return ret;
}


static void
stunCreateErrorResponse(StunMessage* response, int cl, int number, const char* msg)
{
   response->msgHdr.msgType = BindErrorResponseMsg;
   response->hasErrorCode = TRUE;
   response->errorCode.errorClass = cl;
   response->errorCode.number = number;
   strcpy(response->errorCode.reason, msg);
}

#if 0
static void
stunCreateSharedSecretErrorResponse(StunMessage& response, int cl, int number, const char* msg)
{
   response->msgHdr.msgType = SharedSecretErrorResponseMsg;
   response->hasErrorCode = TRUE;
   response->errorCode.errorClass = cl;
   response->errorCode.number = number;
   strcpy(response->errorCode.reason, msg);
}
#endif

static void
stunCreateSharedSecretResponse(const StunMessage* request, const StunAddress4* source, StunMessage* response)
{
   response->msgHdr.msgType = SharedSecretResponseMsg;
   response->msgHdr.id = request->msgHdr.id;
	
   response->hasUsername = TRUE;
   stunCreateUserName( source, &response->username);
	
   response->hasPassword = TRUE;
   stunCreatePassword( &response->username, &response->password);
}


// This funtion takes a single message sent to a stun server, parses
// and constructs an apropriate repsonse - returns TRUE if message is
// valid
BOOL
stunServerProcessMsg( char* buf,
                      unsigned int bufLen,
                      StunAddress4* from, 
                      StunAddress4* secondary,
                      StunAddress4* myAddr,
                      StunAddress4* altAddr, 
                      StunMessage* resp,
                      StunAddress4* destination,
                      StunAtrString* hmacPassword,
                      BOOL* changePort,
                      BOOL* changeIp
                      )
{
    
   // set up information for default response 
	
   memset( resp, 0 , sizeof(*resp) );
	
   *changeIp = FALSE;
   *changePort = FALSE;
	
   StunMessage req;
   BOOL ok = stunParseMessage( buf,bufLen, &req);
	
   if (!ok)      // Complete garbage, drop it on the floor
   {
      dbg_stun("Request did not parse\n");
      return FALSE;
   }
   dbg_stun("Request parsed ok\n");
	
   StunAddress4 mapped = req.mappedAddress.ipv4;
   StunAddress4 respondTo = req.responseAddress.ipv4;
   UInt32 flags = req.changeRequest.value;
	
   switch (req.msgHdr.msgType)
   {
      case SharedSecretRequestMsg:
         dbg_stun("Received SharedSecretRequestMsg on udp. send error 433.\n");
         // !cj! - should fix so you know if this came over TLS or UDP
         stunCreateSharedSecretResponse(&req, from, resp);
         //stunCreateSharedSecretErrorResponse(*resp, 4, 33, "this request must be over TLS");
         return TRUE;
			
      case BindRequestMsg:
         if (!req.hasMessageIntegrity)
         {
             dbg_stun("BindRequest does not contain MessageIntegrity\n");
				
            if (0) // !jf! mustAuthenticate
            {
               dbg_stun("Received BindRequest with no MessageIntegrity. Sending 401.\n");
               stunCreateErrorResponse(resp, 4, 1, "Missing MessageIntegrity");
               return TRUE;
            }
         }
         else
         {
            if (!req.hasUsername)
            {
               dbg_stun("No UserName. Send 432.\n");
               stunCreateErrorResponse(resp, 4, 32, "No UserName and contains MessageIntegrity");
               return TRUE;
            }
            else
            {
               dbg_stun("Validating username: %s\n", req.username.value);
               // !jf! could retrieve associated password from provisioning here
               if (strcmp(req.username.value, "test") == 0)
               {
                  if (0)
                  {
                     // !jf! if the credentials are stale 
                     stunCreateErrorResponse(resp, 4, 30, "Stale credentials on BindRequest");
                     return TRUE;
                  }
                  else
                  {
                     dbg_stun("Validating MessageIntegrity\n");
                     // need access to shared secret
							
                     unsigned char hmac[20];
#ifndef NOSSL
                     unsigned int hmacSize=20;

                     HMAC(EVP_sha1(), 
                          "1234", 4, 
                          reinterpret_cast<const unsigned char*>(buf), bufLen-20-4, 
                          hmac, &hmacSize);
                     assert(hmacSize == 20);
#endif
							
                     if (memcmp(buf, hmac, 20) != 0)
                     {
                        dbg_stun("MessageIntegrity is bad. Sending\n");
                        stunCreateErrorResponse(resp, 4, 3, "Unknown username. Try test with password 1234");
                        return TRUE;
                     }
							
                     // need to compute this later after message is filled in
                     resp->hasMessageIntegrity = TRUE;
                     assert(req.hasUsername);
                     resp->hasUsername = TRUE;
                     resp->username = req.username; // copy username in
                  }
               }
               else
               {
                  dbg_stun("Invalid username: %s. Send 430\n", req.username.value); 
               }
            }
         }
			
         // TODO !jf! should check for unknown attributes here and send 420 listing the
         // unknown attributes. 
			
         if ( respondTo.port == 0 ) respondTo = *from;
         if ( mapped.port == 0 ) mapped = *from;
				
         *changeIp   = ( flags & ChangeIpFlag )?TRUE:FALSE;
         *changePort = ( flags & ChangePortFlag )?TRUE:FALSE;
			
            dbg_stun("Request is valid:\n");
            dbg_stun("\t flags=%d\n", flags);
            dbg_stun("\t changeIp=%d\n", *changeIp);
            dbg_stun("\t changePort=%d\n", *changePort);
            dbg_stun("\t from = %s\n", stunAddress4ToString(from));
            dbg_stun("\t respond to = %s\n", stunAddress4ToString(&respondTo));
            dbg_stun("\t mapped = %s\n", stunAddress4ToString(&mapped));
				
         // form the outgoing message
         resp->msgHdr.msgType = BindResponseMsg;
	 int i;
         for ( i=0; i<16; i++ )
         {
            resp->msgHdr.id.octet[i] = req.msgHdr.id.octet[i];
         }
		
         if ( req.xorOnly == FALSE )
         {
            resp->hasMappedAddress = TRUE;
            resp->mappedAddress.ipv4.port = mapped.port;
            resp->mappedAddress.ipv4.addr = mapped.addr;
         }

         if (1) // do xorMapped address or not 
         {
            resp->hasXorMappedAddress = TRUE;
            UInt16 id16 = req.msgHdr.id.octet[0]<<8 
               | req.msgHdr.id.octet[1];
            UInt32 id32 = req.msgHdr.id.octet[0]<<24 
               | req.msgHdr.id.octet[1]<<16 
               | req.msgHdr.id.octet[2]<<8 
               | req.msgHdr.id.octet[3];
            resp->xorMappedAddress.ipv4.port = mapped.port^id16;
            resp->xorMappedAddress.ipv4.addr = mapped.addr^id32;
         }
         
         resp->hasSourceAddress = TRUE;
         resp->sourceAddress.ipv4.port = (*changePort) ? altAddr->port : myAddr->port;
         resp->sourceAddress.ipv4.addr = (*changeIp)   ? altAddr->addr : myAddr->addr;
			
         resp->hasChangedAddress = TRUE;
         resp->changedAddress.ipv4.port = altAddr->port;
         resp->changedAddress.ipv4.addr = altAddr->addr;
	
         if ( secondary->port != 0 )
         {
            resp->hasSecondaryAddress = TRUE;
            resp->secondaryAddress.ipv4.port = secondary->port;
            resp->secondaryAddress.ipv4.addr = secondary->addr;
         }
         
         if ( req.hasUsername && req.username.sizeValue > 0 ) 
         {
            // copy username in
            resp->hasUsername = TRUE;
            assert( req.username.sizeValue % 4 == 0 );
            assert( req.username.sizeValue < STUN_MAX_STRING );
            memcpy( resp->username.value, req.username.value, req.username.sizeValue );
            resp->username.sizeValue = req.username.sizeValue;
         }
		
         if (1) // add ServerName 
         {
            resp->hasServerName = TRUE;
            const char serverName[] = "Vovida.org " STUN_VERSION; // must pad to mult of 4
            
            assert( sizeof(serverName) < STUN_MAX_STRING );
            assert( sizeof(serverName)%4 == 0 );
            memcpy( resp->serverName.value, serverName, sizeof(serverName));
            resp->serverName.sizeValue = sizeof(serverName);
         }
         
         if ( req.hasMessageIntegrity & req.hasUsername )  
         {
            // this creates the password that will be used in the HMAC when then
            // messages is sent
            stunCreatePassword( &req.username, hmacPassword );
         }
				
         if (req.hasUsername && (req.username.sizeValue > 64 ) )
         {
            UInt32 source;
            assert( sizeof(int) == sizeof(UInt32) );
					
            sscanf(req.username.value, "%x", &source);
            resp->hasReflectedFrom = TRUE;
            resp->reflectedFrom.ipv4.port = 0;
            resp->reflectedFrom.ipv4.addr = source;
         }
				
         destination->port = respondTo.port;
         destination->addr = respondTo.addr;
			
         return TRUE;
			
      default:
         dbg_stun("Unknown or unsupported request \n");
         return FALSE;
   }
	
   assert(0);
   return FALSE;
}

BOOL
stunInitServer(StunServerInfo* info, const StunAddress4* myAddr,
               const StunAddress4* altAddr, int startMediaPort)
{
	assert( myAddr->port != 0 );
	assert( altAddr->port!= 0 );
	assert( myAddr->addr  != 0 );
	//assert( altAddr.addr != 0 );

	info->myAddr = *myAddr;
	info->altAddr = *altAddr;

	info->myFd = INVALID_SOCKET;
	info->altPortFd = INVALID_SOCKET;
	info->altIpFd = INVALID_SOCKET;
	info->altIpPortFd = INVALID_SOCKET;

	memset(info->relays, 0, sizeof(info->relays));
	if (startMediaPort > 0)
	{
		info->relay = TRUE;
		int i;
		for (i=0; i<MAX_MEDIA_RELAYS; ++i)
		{
			StunMediaRelay* relay = &info->relays[i];
			relay->relayPort = startMediaPort+i;
			relay->fd = 0;
			relay->expireTime = 0;
		}
	}
	else
	{
		info->relay = FALSE;
	}

	if ((info->myFd = openPort(myAddr->port, myAddr->addr)) == INVALID_SOCKET)
	{
		dbg_stun("Can't open %s\n", stunAddress4ToString(myAddr));
		stunStopServer(info);

		return FALSE;
	}

	if ((info->altPortFd = openPort(altAddr->port,myAddr->addr)) == INVALID_SOCKET)
	{
		dbg_stun("Can't open %s\n", stunAddress4ToString(myAddr));
		stunStopServer(info);
		return FALSE;
	}


	info->altIpFd = INVALID_SOCKET;
	if (  altAddr->addr != 0 )
	{
		if ((info->altIpFd = openPort( myAddr->port, altAddr->addr)) == INVALID_SOCKET)
		{
			dbg_stun("Can't open %s\n", stunAddress4ToString(altAddr));
			stunStopServer(info);
			return FALSE;
		}
	}

	info->altIpPortFd = INVALID_SOCKET;
	if (  altAddr->addr != 0 )
	{  if ((info->altIpPortFd = openPort(altAddr->port, altAddr->addr)) == INVALID_SOCKET)
		{
			dbg_stun("Can't open %s\n", stunAddress4ToString(altAddr));
			stunStopServer(info);
			return FALSE;
		}
	}

	return TRUE;
}

void
stunStopServer(StunServerInfo* info)
{
	if (info->myFd > 0) PA_SocketClose(info->myFd);
	if (info->altPortFd > 0) PA_SocketClose(info->altPortFd);
	if (info->altIpFd > 0) PA_SocketClose(info->altIpFd);
	if (info->altIpPortFd > 0) PA_SocketClose(info->altIpPortFd);

	if (info->relay)
	{
		int i;
		for (i=0; i<MAX_MEDIA_RELAYS; ++i)
		{
			StunMediaRelay* relay = &info->relays[i];
			if (relay->fd)
			{
				PA_SocketClose(relay->fd);
				relay->fd = 0;
			}
		}
	}
}


BOOL
stunServerProcess(StunServerInfo* info)
{
	char msg[STUN_MAX_MESSAGE_SIZE];
	int msgLen = sizeof(msg);

	BOOL ok = FALSE;
	BOOL recvAltIp =FALSE;
	BOOL recvAltPort = FALSE;

	fd_set fdSet; 
	PA_SOCKET maxFd=0;

	FD_ZERO(&fdSet); 
	FD_SET(info->myFd,&fdSet); 
	if ( info->myFd >= maxFd ) maxFd=info->myFd+1;
	FD_SET(info->altPortFd,&fdSet); 
	if ( info->altPortFd >= maxFd ) maxFd=info->altPortFd+1;

	if ( info->altIpFd != INVALID_SOCKET )
	{
		FD_SET(info->altIpFd,&fdSet);
		if (info->altIpFd>=maxFd) maxFd=info->altIpFd+1;
	}
	if ( info->altIpPortFd != INVALID_SOCKET )
	{
		FD_SET(info->altIpPortFd,&fdSet);
		if (info->altIpPortFd>=maxFd) maxFd=info->altIpPortFd+1;
	}

	if (info->relay)
	{
		int i;
		for (i=0; i<MAX_MEDIA_RELAYS; ++i)
		{
			StunMediaRelay* relay = &info->relays[i];
			if (relay->fd)
			{
				FD_SET(relay->fd, &fdSet);
				if (relay->fd >= maxFd) 
				{
					maxFd=relay->fd+1;
				}
			}
		}
	}

	if ( info->altIpFd != INVALID_SOCKET )
	{
		FD_SET(info->altIpFd,&fdSet);
		if (info->altIpFd>=maxFd) maxFd=info->altIpFd+1;
	}
	if ( info->altIpPortFd != INVALID_SOCKET )
	{
		FD_SET(info->altIpPortFd,&fdSet);
		if (info->altIpPortFd>=maxFd) maxFd=info->altIpPortFd+1;
	}

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;

	int e = select( maxFd, &fdSet, NULL,NULL, &tv );
	if (e < 0)
	{
		dbg_stun("Error on select: %s\n", strerror(PA_SocketGetError()));
	}
	else if (e >= 0)
	{
		StunAddress4 from;

		// do the media relaying
		if (info->relay)
		{
			time_t now = time(0);
			int i;
			for (i=0; i<MAX_MEDIA_RELAYS; ++i)
			{
				StunMediaRelay* relay = &info->relays[i];
				if (relay->fd)
				{
					if (FD_ISSET(relay->fd, &fdSet))
					{
						char msg[MAX_RTP_MSG_SIZE];
						int msgLen = sizeof(msg);

						StunAddress4 rtpFrom;
						ok = getMessage( relay->fd, msg, &msgLen, &rtpFrom.addr, &rtpFrom.port);
						if (ok)
						{
							sendMessage(info->myFd, msg, msgLen, relay->destination.addr, relay->destination.port);
							relay->expireTime = now + MEDIA_RELAY_TIMEOUT;
						}
					}
					else if (now > relay->expireTime)
					{
						PA_SocketClose(relay->fd);
						relay->fd = 0;
					}
				}
			}
		}


		if (FD_ISSET(info->myFd,&fdSet))
		{
			dbg_stun("received on A1:P1\n");
			recvAltIp = FALSE;
			recvAltPort = FALSE;
			ok = getMessage( info->myFd, msg, &msgLen, &from.addr, &from.port);
		}
		else if (FD_ISSET(info->altPortFd, &fdSet))
		{
			dbg_stun("received on A1:P2\n");
			recvAltIp = FALSE;
			recvAltPort = TRUE;
			ok = getMessage( info->altPortFd, msg, &msgLen, &from.addr, &from.port);
		}
		else if ( (info->altIpFd!=INVALID_SOCKET) && FD_ISSET(info->altIpFd,&fdSet))
		{
			dbg_stun("received on A2:P1\n");
			recvAltIp = TRUE;
			recvAltPort = FALSE;
			ok = getMessage( info->altIpFd, msg, &msgLen, &from.addr, &from.port);
		}
		else if ( (info->altIpPortFd!=INVALID_SOCKET) && FD_ISSET(info->altIpPortFd, &fdSet))
		{
			dbg_stun("received on A2:P2\n");
			recvAltIp = TRUE;
			recvAltPort = TRUE;
			ok = getMessage( info->altIpPortFd, msg, &msgLen, &from.addr, &from.port);
		}
		else
		{
			return TRUE;
		}

		int relayPort = 0;
		if (info->relay)
		{
			int i;
			for (i=0; i<MAX_MEDIA_RELAYS; ++i)
			{
				StunMediaRelay* relay = &info->relays[i];
				if (relay->destination.addr == from.addr && 
						relay->destination.port == from.port)
				{
					relayPort = relay->relayPort;
					relay->expireTime = time(0) + MEDIA_RELAY_TIMEOUT;
					break;
				}
			}

			if (relayPort == 0)
			{
				for (i=0; i<MAX_MEDIA_RELAYS; ++i)
				{
					StunMediaRelay* relay = &info->relays[i];
					if (relay->fd == 0)
					{
						dbg_stun("Open relay port %d\n", relay->relayPort);

						relay->fd = openPort(relay->relayPort, info->myAddr.addr);
						relay->destination.addr = from.addr;
						relay->destination.port = from.port;
						relay->expireTime = time(0) + MEDIA_RELAY_TIMEOUT;
						relayPort = relay->relayPort;
						break;
					}
				}
			}
		}

		if ( !ok ) 
		{
			dbg_stun("Get message did not return a valid message\n");
			return TRUE;
		}

		dbg_stun("Got a request (len=%d) from %x\n", msgLen, from.addr);

		if ( msgLen <= 0 )
		{
			return TRUE;
		}

		BOOL changePort = FALSE;
		BOOL changeIp = FALSE;

		StunMessage resp;
		StunAddress4 dest;
		StunAtrString hmacPassword;  
		hmacPassword.sizeValue = 0;

		StunAddress4 secondary;
		secondary.port = 0;
		secondary.addr = 0;

		if (info->relay && relayPort)
		{
			secondary = from;

			from.addr = info->myAddr.addr;
			from.port = relayPort;
		}

		ok = stunServerProcessMsg( msg, msgLen, &from, &secondary,
				recvAltIp ? &info->altAddr : &info->myAddr,
				recvAltIp ? &info->myAddr : &info->altAddr, 
				&resp,
				&dest,
				&hmacPassword,
				&changePort,
				&changeIp
				);

		if ( !ok )
		{
			dbg_stun("Failed to parse message\n");
			return TRUE;
		}

		char buf[STUN_MAX_MESSAGE_SIZE];
		int len = sizeof(buf);

		len = stunEncodeMessage( &resp, buf, len, &hmacPassword);

		if ( dest.addr == 0 )  ok=FALSE;
		if ( dest.port == 0 ) ok=FALSE;

		if ( ok )
		{
			assert( dest.addr != 0 );
			assert( dest.port != 0 );

			PA_SOCKET sendFd;

			BOOL sendAltIp   = recvAltIp;   // send on the received IP address 
			BOOL sendAltPort = recvAltPort; // send on the received port

			if ( changeIp )   sendAltIp   = !sendAltIp;   // if need to change IP, then flip logic 
			if ( changePort ) sendAltPort = !sendAltPort; // if need to change port, then flip logic 

			if ( !sendAltPort )
			{
				if ( !sendAltIp )
				{
					sendFd = info->myFd;
				}
				else
				{
					sendFd = info->altIpFd;
				}
			}
			else
			{
				if ( !sendAltIp )
				{
					sendFd = info->altPortFd;
				}
				else
				{
					sendFd = info->altIpPortFd;
				}
			}

			if ( sendFd != INVALID_SOCKET )
			{
				sendMessage( sendFd, buf, len, dest.addr, dest.port);
			}
		}
	}

	return TRUE;
}

int 
stunFindLocalInterfaces(UInt32* addresses,int maxRet)
{
#if defined(WIN32) || defined(__sparc__)
   return 0;
#else
   struct ifconf ifc;
	
   int s = socket( AF_INET, SOCK_DGRAM, 0 );
   int len = 100 * sizeof(struct ifreq);
	
   char buf[ len ];
	
   ifc.ifc_len = len;
   ifc.ifc_buf = buf;
	
   int e = ioctl(s,SIOCGIFCONF,&ifc);
   char *ptr = buf;
   int tl = ifc.ifc_len;
   int count=0;
	
   while ( (tl > 0) && ( count < maxRet) )
   {
      struct ifreq* ifr = (struct ifreq *)ptr;
		
      int si = sizeof(ifr->ifr_name) + sizeof(struct sockaddr);
      tl -= si;
      ptr += si;
      //char* name = ifr->ifr_ifrn.ifrn_name;
      //cerr << "name = " << name << );
		
      struct ifreq ifr2;
      ifr2 = *ifr;
		
      e = ioctl(s,SIOCGIFADDR,&ifr2);
      if ( e == -1 )
      {
         break;
      }
		
      //cerr << "ioctl addr e = " << e << );
		
      struct sockaddr a = ifr2.ifr_addr;
      struct sockaddr_in* addr = (struct sockaddr_in*) &a;
		
      UInt32 ai = ntohl( addr->sin_addr.s_addr );
      if ((int)((ai>>24)&0xFF) != 127)
      {
         addresses[count++] = ai;
      }
		
#if 0
      cerr << "Detected interface "
           << (int)((ai>>24)&0xFF) << "." 
           << (int)((ai>>16)&0xFF) << "." 
           << (int)((ai>> 8)&0xFF) << "." 
           << (int)((ai    )&0xFF) << );
#endif
   }
	
   PA_SocketClose(s);
	
   return count;
#endif
}


void
stunBuildReqSimple( StunMessage* msg,
                    const StunAtrString* username,
                    BOOL changePort, BOOL changeIp, unsigned int id )
{
   assert( msg );
   memset( msg , 0 , sizeof(*msg) );
	
   msg->msgHdr.msgType = BindRequestMsg;
	
   int i;
   for (i=0; i<16; i=i+4 )
   {
      assert(i+3<16);
      int r = stunRand();
      msg->msgHdr.id.octet[i+0]= r>>0;
      msg->msgHdr.id.octet[i+1]= r>>8;
      msg->msgHdr.id.octet[i+2]= r>>16;
      msg->msgHdr.id.octet[i+3]= r>>24;
   }
	
   if ( id != 0 )
   {
      msg->msgHdr.id.octet[0] = id; 
   }
	
   msg->hasChangeRequest = TRUE;
   msg->changeRequest.value =(changeIp?ChangeIpFlag:0) | 
      (changePort?ChangePortFlag:0);
	
   if ( username->sizeValue > 0 )
   {
      msg->hasUsername = TRUE;
      msg->username = *username;
   }
}


static void 
stunSendTest( PA_SOCKET myFd, const StunAddress4* dest, 
              const StunAtrString* username, const StunAtrString* password, 
              int testNum)
{ 
   assert( dest->addr != 0 );
   assert( dest->port != 0 );
	
   BOOL changePort=FALSE;
   BOOL changeIP=FALSE;
   BOOL discard=FALSE;
	
   switch (testNum)
   {
      case 1:
      case 10:
      case 11:
         break;
      case 2:
         //changePort=TRUE;
         changeIP=TRUE;
         break;
      case 3:
         changePort=TRUE;
         break;
      case 4:
         changeIP=TRUE;
         break;
      case 5:
         discard=TRUE;
         break;
      default:
         fprintf(stderr, "Test %d is unkown\n", testNum);
         assert(0);
   }
	
   StunMessage req;
   memset(&req, 0, sizeof(StunMessage));
	
   stunBuildReqSimple( &req, username, 
                       changePort , changeIP , 
                       testNum );
	
   char buf[STUN_MAX_MESSAGE_SIZE];
   int len = STUN_MAX_MESSAGE_SIZE;
	
   len = stunEncodeMessage( &req, buf, len, password);
	
   sendMessage( myFd, buf, len, dest->addr, dest->port);
	
   // add some delay so the packets don't get sent too quickly 
#ifdef WIN32 // !cj! TODO - should fix this up in windows
		 clock_t now = clock();
		 assert( CLOCKS_PER_SEC == 1000 );
		 while ( clock() <= now+10 ) { };
#else
		 usleep(10*1000);
#endif

}


void 
stunGetUserNameAndPassword(  const StunAddress4* dest, 
                             StunAtrString* username,
                             StunAtrString* password)
{ 
	// !cj! This is totally bogus - need to make TLS connection to dest and get a
	// username and password to use 
	stunCreateUserName(dest, username);
	stunCreatePassword(username, password);
}


NatType
stunNatType( const StunAddress4* dest, 
             BOOL* preservePort, // if set, is return for if NAT preservers ports or not
             BOOL* hairpin,  // if set, is the return for if NAT will hairpin packets
             int port, // port to use for the test, 0 to choose random port
             StunAddress4* sAddr // NIC to use 
   )
{ 
	assert( dest->addr != 0 );
	assert( dest->port != 0 );

	if ( hairpin ) 
	{
		*hairpin = FALSE;
	}

	if ( port == 0 )
	{
		port = stunRandomPort();
	}
	UInt32 interfaceIp=0;
	if (sAddr)
	{
		interfaceIp = sAddr->addr;
	}
	PA_SOCKET myFd1 = openPort(port,interfaceIp);
	PA_SOCKET myFd2 = openPort(port+1,interfaceIp);

	if ( ( myFd1 == INVALID_SOCKET) || ( myFd2 == INVALID_SOCKET) )
	{
		fprintf(stderr, "Some problem opening port/interface to send on\n");
		return StunTypeFailure; 
	}

	assert( myFd1 != INVALID_SOCKET );
	assert( myFd2 != INVALID_SOCKET );

	BOOL respTestI=FALSE;
	BOOL isNat=TRUE;
	StunAddress4 testIchangedAddr;
	StunAddress4 testImappedAddr;
	BOOL respTestI2=FALSE; 
	BOOL mappedIpSame = TRUE;
	StunAddress4 testI2mappedAddr;
	StunAddress4 testI2dest=*dest;
	BOOL respTestII=FALSE;
	BOOL respTestIII=FALSE;

	BOOL respTestHairpin=FALSE;
	BOOL respTestPreservePort=FALSE;

	memset(&testImappedAddr,0,sizeof(testImappedAddr));

	StunAtrString username;
	StunAtrString password;

	username.sizeValue = 0;
	password.sizeValue = 0;

#ifdef USE_TLS 
	stunGetUserNameAndPassword( dest, username, password );
#endif

	int count=0;
	while ( count < 7 )
	{
		struct timeval tv;
		fd_set fdSet; 
#ifdef WIN32
		unsigned int fdSetSize;
#else
		int fdSetSize;
#endif
		FD_ZERO(&fdSet); fdSetSize=0;
		FD_SET(myFd1,&fdSet); fdSetSize = (myFd1+1>fdSetSize) ? myFd1+1 : fdSetSize;
		FD_SET(myFd2,&fdSet); fdSetSize = (myFd2+1>fdSetSize) ? myFd2+1 : fdSetSize;
		tv.tv_sec=0;
		tv.tv_usec=150*1000; // 150 ms 
		if ( count == 0 ) tv.tv_usec=0;
		int  err = select(fdSetSize, &fdSet, NULL, NULL, &tv);
		if ( err == PA_SOCKET_ERROR )
		{
			// error occured
			int e = PA_SocketGetError();
			fprintf(stderr, "Error %d %s in select\n", e, strerror(e));
			return StunTypeFailure; 
		}
		else if ( err == 0 )
		{
			// timeout occured 
			count++;

			if ( !respTestI ) 
			{
				stunSendTest( myFd1, dest, &username, &password, 1);
			}         
			if ( (!respTestI2) && respTestI ) 
			{
				// check the address to send to if valid 
				if (  ( testI2dest.addr != 0 ) &&
						( testI2dest.port != 0 ) )
				{
					stunSendTest( myFd1, &testI2dest, &username, &password, 10);
				}
			}

			if ( !respTestII )
			{
				stunSendTest( myFd2, dest, &username, &password, 2);
			}

			if ( !respTestIII )
			{
				stunSendTest( myFd2, dest, &username, &password, 3);
			}

			if ( respTestI && (!respTestHairpin) )
			{
				if (  ( testImappedAddr.addr != 0 ) &&
						( testImappedAddr.port != 0 ) )
				{
					stunSendTest( myFd1, &testImappedAddr, &username, &password, 11);
				}
			}
		}
		else
		{
			assert( err>0 );
			// data is avialbe on some fd 

			int i;
			for (i=0; i<2; i++)
			{
				PA_SOCKET myFd;
				if ( i==0 ) 
				{
					myFd=myFd1;
				}
				else
				{
					myFd=myFd2;
				}

				if ( myFd!=INVALID_SOCKET ) 
				{					
					if ( FD_ISSET(myFd,&fdSet) )
					{
						char msg[STUN_MAX_MESSAGE_SIZE];
						int msgLen = sizeof(msg);

						StunAddress4 from;

						getMessage( myFd, msg, &msgLen, &from.addr, &from.port);

						StunMessage resp;
						memset(&resp, 0, sizeof(StunMessage));

						stunParseMessage( msg,msgLen, &resp);

						dbg_stun("Received message of type %d  id=%d\n", resp.msgHdr.msgType,
								(int)(resp.msgHdr.id.octet[0]));

						switch( resp.msgHdr.id.octet[0] )
						{
							case 1:
								if ( !respTestI )
								{

									testIchangedAddr.addr = resp.changedAddress.ipv4.addr;
									testIchangedAddr.port = resp.changedAddress.ipv4.port;
									testImappedAddr.addr = resp.mappedAddress.ipv4.addr;
									testImappedAddr.port = resp.mappedAddress.ipv4.port;

									respTestPreservePort = ( testImappedAddr.port == port ); 
									if ( preservePort )
									{
										*preservePort = respTestPreservePort;
									}								

									testI2dest.addr = resp.changedAddress.ipv4.addr;

									if (sAddr)
									{
										sAddr->port = testImappedAddr.port;
										sAddr->addr = testImappedAddr.addr;
									}

									count = 0;
								}		
								respTestI=TRUE;
								break;
							case 2:
								respTestII=TRUE;
								break;
							case 3:
								respTestIII=TRUE;
								break;
							case 10:
								if ( !respTestI2 )
								{
									testI2mappedAddr.addr = resp.mappedAddress.ipv4.addr;
									testI2mappedAddr.port = resp.mappedAddress.ipv4.port;

									mappedIpSame = FALSE;
									if ( (testI2mappedAddr.addr  == testImappedAddr.addr ) &&
											(testI2mappedAddr.port == testImappedAddr.port ))
									{ 
										mappedIpSame = TRUE;
									}
								}
								respTestI2=TRUE;
								break;
							case 11:
								if ( hairpin ) 
								{
									*hairpin = TRUE;
								}
								respTestHairpin = TRUE;
								break;
						}
					}
				}
			}
		}
	}

	PA_SocketClose(myFd1);
	PA_SocketClose(myFd2);

	// see if we can bind to this address 
	//cerr << "try binding to " << testImappedAddr << );
	PA_SOCKET s = openPort( 0/*use ephemeral*/, testImappedAddr.addr);
	if ( s != INVALID_SOCKET )
	{
		PA_SocketClose(s);
		isNat = FALSE;
		//cerr << "binding worked" << );
	}
	else
	{
		isNat = TRUE;
		//cerr << "binding failed" << );
	}

	dbg_stun("test I = %d\n", respTestI);
	dbg_stun("test II = %d\n", respTestII);
	dbg_stun("test III = %d\n", respTestIII);
	dbg_stun("test I(2) = %d\n", respTestI2);
	dbg_stun("is nat  = %d\n", isNat);
	dbg_stun("mapped IP same = %d", mappedIpSame);
	dbg_stun("hairpin = %d", respTestHairpin);
	dbg_stun("preserver port = %d", respTestPreservePort);

#if 0
	// implement logic flow chart from draft RFC
	if ( respTestI )
	{
		if ( isNat )
		{
			if (respTestII)
			{
				return StunTypeConeNat;
			}
			else
			{
				if ( mappedIpSame )
				{
					if ( respTestIII )
					{
						return StunTypeRestrictedNat;
					}
					else
					{
						return StunTypePortRestrictedNat;
					}
				}
				else
				{
					return StunTypeSymNat;
				}
			}
		}
		else
		{
			if (respTestII)
			{
				return StunTypeOpen;
			}
			else
			{
				return StunTypeSymFirewall;
			}
		}
	}
	else
	{
		return StunTypeBlocked;
	}
#else
	if ( respTestI ) // not blocked 
	{
		if ( isNat )
		{
			if ( mappedIpSame )
			{
				if (respTestII)
				{
					return StunTypeIndependentFilter;
				}
				else
				{
					if ( respTestIII )
					{
						return StunTypeDependentFilter;
					}
					else
					{
						return StunTypePortDependedFilter;
					}
				}
			}
			else // mappedIp is not same 
			{
				return StunTypeDependentMapping;
			}
		}
		else  // isNat is FALSE
		{
			if (respTestII)
			{
				return StunTypeOpen;
			}
			else
			{
				return StunTypeFirewall;
			}
		}
	}
	else
	{
		return StunTypeBlocked;
	}
#endif

	return StunTypeUnknown;
}


int
stunOpenSocket( StunAddress4* dest, StunAddress4* mapAddr, 
                int port, StunAddress4* srcAddr)
{
	assert( dest->addr != 0 );
	assert( dest->port != 0 );
	assert( mapAddr );

	if ( port == 0 )
	{
		port = stunRandomPort();
	}
	unsigned int interfaceIp = 0;
	if ( srcAddr )
	{
		interfaceIp = srcAddr->addr;
	}

	PA_SOCKET myFd = openPort(port,interfaceIp);
	if (myFd == INVALID_SOCKET)
	{
		return myFd;
	}

	char msg[STUN_MAX_MESSAGE_SIZE];
	int msgLen = sizeof(msg);

	StunAtrString username;
	StunAtrString password;

	username.sizeValue = 0;
	password.sizeValue = 0;

#ifdef USE_TLS
	stunGetUserNameAndPassword( dest, username, password );
#endif

	stunSendTest(myFd, dest, &username, &password, 1);

	StunAddress4 from;

	getMessage( myFd, msg, &msgLen, &from.addr, &from.port);

	StunMessage resp;
	memset(&resp, 0, sizeof(StunMessage));

	BOOL ok = stunParseMessage( msg, msgLen, &resp);
	if (!ok)
	{
		return -1;
	}

	*mapAddr = resp.mappedAddress.ipv4;

	return myFd;
}


BOOL
stunOpenSocketPair( StunAddress4* dest, StunAddress4* mapAddr, 
                    int* fd1, int* fd2, 
                    int port, StunAddress4* srcAddr
                    )
{
	assert( dest->addr!= 0 );
	assert( dest->port != 0 );
	assert( mapAddr );

	const int NUM=3;

	if ( port == 0 )
	{
		port = stunRandomPort();
	}

	*fd1=-1;
	*fd2=-1;

	char msg[STUN_MAX_MESSAGE_SIZE];
	int msgLen =sizeof(msg);

	StunAddress4 from;
	int fd[NUM];
	int i;

	unsigned int interfaceIp = 0;
	if ( srcAddr )
	{
		interfaceIp = srcAddr->addr;
	}

	for( i=0; i<NUM; i++)
	{
		fd[i] = openPort( (port == 0) ? 0 : (port + i), 
				interfaceIp);
		if (fd[i] < 0) 
		{
			while (i > 0)
			{
				PA_SocketClose(fd[--i]);
			}
			return FALSE;
		}
	}

	StunAtrString username;
	StunAtrString password;

	username.sizeValue = 0;
	password.sizeValue = 0;

#ifdef USE_TLS
	stunGetUserNameAndPassword( dest, username, password );
#endif

	for( i=0; i<NUM; i++)
	{
		stunSendTest(fd[i], dest, &username, &password, 1/*testNum*/ );
	}

	StunAddress4 mappedAddr[NUM];
	for( i=0; i<NUM; i++)
	{
		msgLen = sizeof(msg)/sizeof(*msg);
		getMessage( fd[i],
				msg,
				&msgLen,
				&from.addr,
				&from.port);

		StunMessage resp;
		memset(&resp, 0, sizeof(StunMessage));

		BOOL ok = stunParseMessage( msg, msgLen, &resp);
		if (!ok) 
		{
			return FALSE;
		}

		mappedAddr[i] = resp.mappedAddress.ipv4;
	}

	dbg_stun("--- stunOpenPA_SOCKETPair --- \n");
	for( i=0; i<NUM; i++)
	{
		dbg_stun("\t mappedAddr=%s", stunAddress4ToString(&mappedAddr[i]));
	}

	if ( mappedAddr[0].port %2 == 0 )
	{
		if (  mappedAddr[0].port+1 ==  mappedAddr[1].port )
		{
			*mapAddr = mappedAddr[0];
			*fd1 = fd[0];
			*fd2 = fd[1];
			PA_SocketClose( fd[2] );
			return TRUE;
		}
	}
	else
	{
		if (( mappedAddr[1].port %2 == 0 )
				&& (  mappedAddr[1].port+1 ==  mappedAddr[2].port ))
		{
			*mapAddr = mappedAddr[1];
			*fd1 = fd[1];
			*fd2 = fd[2];
			PA_SocketClose( fd[0] );
			return TRUE;
		}
	}

	// something failed, close all and return error
	for( i=0; i<NUM; i++)
	{
		PA_SocketClose( fd[i] );
	}

	return FALSE;
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */

// Local Variables:
// mode:c++
// c-file-style:"ellemtel"
// c-file-offsets:((case-label . +))
// indent-tabs-mode:nil
// End:


