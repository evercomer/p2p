#include <assert.h>
#include "udp.h"

#ifdef __LINUX__
#define dbg_stun(fmt, args...) 
#elif defined(WIN32)
#define dbg_stun(fmt, __VA_ARGS__)
#endif

PA_SOCKET
openPort( unsigned short port, unsigned int interfaceIp)
{
	PA_SOCKET fd;
	struct sockaddr_in addr;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if ( fd == INVALID_SOCKET )
	{
		dbg_stun("Could not create a UDP socket: %d\n", PA_SocketGetError());
		return INVALID_SOCKET;
	}

	memset((char*) &(addr),0, sizeof((addr)));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if ( (interfaceIp != 0) && 
			( interfaceIp != 0x100007f ) )
	{
		addr.sin_addr.s_addr = htonl(interfaceIp);
		dbg_stun("Binding to interface 0x%X\n", htonl(interfaceIp));
	}

	if ( bind( fd,(struct sockaddr*)&addr, sizeof(addr)) != 0 )
	{
		int e = PA_SocketGetError();

		switch (e)
		{
			case 0:
				{
					dbg_stun("Could not bind socket\n");
					return INVALID_SOCKET;
				}
			case EADDRINUSE:
				{
					dbg_stun("Port %d for receiving UDP is in use\n", port);
					return INVALID_SOCKET;
				}
				break;
			case EADDRNOTAVAIL:
				{
					dbg_stun("Cannot assign requested address\n");
					return INVALID_SOCKET;
				}
				break;
			default:
				{
					fprintf(stderr, "Could not bind UDP receive port. Error=%s\n", strerror(e));
					return INVALID_SOCKET;
				}
				break;
		}
	}
	dbg_stun("Opened port %d with fd %d\n", port, fd);

	assert( fd != INVALID_SOCKET  );

	return fd;
}


BOOL 
getMessage( PA_SOCKET fd, char* buf, int* len,
            unsigned int* srcIp, unsigned short* srcPort)
{
	int originalSize;
	struct sockaddr_in from;
	int fromLen;

	assert( fd != INVALID_SOCKET );

	originalSize = *len;
	assert( originalSize > 0 );

	fromLen = sizeof(from);

	*len = recvfrom(fd,
			buf,
			originalSize,
			0,
			(struct sockaddr *)&from,
			(socklen_t*)&fromLen);

	if ( *len == PA_SOCKET_ERROR )
	{
		int err = PA_SocketGetError();

		switch (err)
		{
			case ENOTSOCK:
				dbg_stun("Error fd not a socket\n");
				break;
			case ECONNRESET:
				dbg_stun("Error connection reset - host not reachable\n");
				break;

			default:
				fprintf(stderr, "PA_SOCKET Error=%d\n", err);
		}

		return FALSE;
	}

	if ( *len < 0 )
	{
		dbg_stun("socket closed? negative len\n");
		return FALSE;
	}

	if ( *len == 0 )
	{
		dbg_stun("socket closed? zero len\n");
		return FALSE;
	}

	*srcPort = ntohs(from.sin_port);
	*srcIp = ntohl(from.sin_addr.s_addr);

	if ( (*len)+1 >= originalSize )
	{
		dbg_stun("Received a message that was too large\n");
		return FALSE;
	}
	buf[*len]=0;

	return TRUE;
}


BOOL sendMessage( PA_SOCKET fd, char* buf, int l, 
             unsigned int dstIp, unsigned short dstPort)
{
	int s;
	assert( fd != INVALID_SOCKET );

	if ( dstPort == 0 )
	{
		// sending on a connected port 
		assert( dstIp == 0 );

		s = send(fd,buf,l,0);
	}
	else
	{
		struct sockaddr_in to;
		int toLen;

		assert( dstIp != 0 );
		assert( dstPort != 0 );

		toLen = sizeof(to);
		memset(&to,0,toLen);

		to.sin_family = AF_INET;
		to.sin_port = htons(dstPort);
		to.sin_addr.s_addr = htonl(dstIp);

		s = PA_SendTo(fd, buf, l, 0, (struct sockaddr*)&to, toLen);
	}

	if ( s == PA_SOCKET_ERROR )
	{
		int e = PA_SocketGetError();
		switch (e)
		{
			case ECONNREFUSED:
			case EHOSTDOWN:
			case EHOSTUNREACH:
				{
					// quietly ignore this 
				}
				break;
			case EAFNOSUPPORT:
				{
					fprintf(stderr, "err EAFNOSUPPORT in send\n");
				}
				break;
			default:
				{
					fprintf(stderr, "err %d %s in send\n", e, strerror(e));
				}
		}
		return FALSE;
	}

	if ( s == 0 )
	{
		fprintf(stderr, "no data sent in send\n");
		return FALSE;
	}

	if ( s != l )
	{
		fprintf(stderr, "only %d out of %d bytes sent\n", s, l);
		return FALSE;
	}

	return TRUE;
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
