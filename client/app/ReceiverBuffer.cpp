#include "ReceiverBuffer.h"
#include <stdlib.h>
#include "misc.h"

#ifndef SAFE_FREE
#define SAFE_FREE(p) if(p) { free(p); p = NULL; }
#endif

CReceiverBuffer::CReceiverBuffer()
{
	m_pBuffer = NULL;
	m_pNodes = m_pNewestNode = NULL;
	m_rLatestKeyFrame = m_rPrerecordStart = m_rIdx = m_wIdx = 0;
}
CReceiverBuffer::CReceiverBuffer(UINT maxFrameSize, UINT nBuffer, UINT nNode)
{
	m_pBuffer = NULL;
	m_pNodes = NULL;
	m_rLatestKeyFrame = m_rPrerecordStart = m_rIdx = m_wIdx = 0;
	AllocateBuffer(maxFrameSize, nBuffer, nNode);
}
CReceiverBuffer::~CReceiverBuffer()
{
	FreeBuffer();
}

void CReceiverBuffer::AllocateBuffer(UINT maxFrameSize, UINT nBuffer, UINT nNode)
{
	SAFE_FREE(m_pBuffer);
	SAFE_FREE(m_pNodes);
	
	if(nBuffer < 3) nBuffer = 3;
	if(nNode < 3*nBuffer) nNode = 3*nBuffer;
	m_dwBufferSize = maxFrameSize * nBuffer;
	m_pBuffer = (BYTE*)malloc(m_dwBufferSize);
	m_dwMaxFrameSize = maxFrameSize;

	m_dwCount = nNode;
	m_pNodes = (FRAMENODE*)calloc(sizeof(FRAMENODE), nNode);
	m_pNewestNode = NULL;
	m_rLatestKeyFrame = m_rPrerecordStart = m_rIdx = m_wIdx = 0;
	m_pNodes[0].pData = m_pBuffer;
	m_bFull = FALSE;
}
void CReceiverBuffer::FreeBuffer()
{
	SAFE_FREE(m_pBuffer);
	SAFE_FREE(m_pNodes);
	m_rLatestKeyFrame = m_rPrerecordStart = m_rIdx = m_wIdx = 0;
}

FRAMENODE *CReceiverBuffer::GetFrameToWrite()
{
	if( (m_wIdx + 1) % m_dwCount == m_rIdx) 
	{ 
		m_bFull = TRUE; 
		return NULL;	//safe reservation
	}
	if(m_pNodes[m_wIdx].pData == m_pNodes[m_rIdx].pData && m_wIdx != m_rIdx)	//Full
	{
		m_bFull = TRUE;
		return NULL;
	}
	if(m_pNodes[m_wIdx].pData < m_pNodes[m_rIdx].pData &&
		m_pNodes[m_wIdx].pData + m_dwMaxFrameSize > m_pNodes[m_rIdx].pData) 
	{
		m_bFull = TRUE;
		return NULL;
	}

	m_bFull = FALSE;
	return &m_pNodes[m_wIdx];
}

BOOL CReceiverBuffer::ReserveSpace(UINT len)
{
	if(m_pNodes[m_wIdx].pData < m_pNodes[m_rIdx].pData &&
		m_pNodes[m_wIdx].pData + len >= m_pNodes[m_rIdx].pData) { m_bFull = TRUE; return FALSE; }

		m_bFull = FALSE;
	if(m_pNodes[m_wIdx].pData < m_pNodes[m_rPrerecordStart].pData &&
			m_pNodes[m_wIdx].pData + len >= m_pNodes[m_rPrerecordStart].pData)
	{
		do {
			m_rPrerecordStart = (m_rPrerecordStart + 1)%m_dwCount;
		} while(!m_pNodes[m_rPrerecordStart].isKeyFrame && m_rPrerecordStart != m_wIdx);
	}

	return TRUE;
}

FRAMENODE *CReceiverBuffer::NextFrame(FRAMENODE *pNode)
{
	int index = (pNode - m_pNodes);
	if(index == m_wIdx) return NULL;

	index = (index + 1) % m_dwCount;
	return &m_pNodes[index];
}

void CReceiverBuffer::QueueFrame(FRAMENODE *pNode)
{
	if(pNode)
	{
		int index = (pNode - m_pNodes);
		if(index == m_wIdx)
		{
			if(pNode->isKeyFrame)	//Ô¤Â¼Ö§³Ö
			{
				if(pNode->timeStamp - m_pNodes[m_rPrerecordStart].timeStamp > 5000)
				{
					DWORD tmp = m_rPrerecordStart;
					do {
						tmp = (tmp + 1) % m_dwCount;
						if( (m_pNodes[tmp].isKeyFrame) && (pNode->timeStamp - m_pNodes[tmp].timeStamp > 5000) )
							m_rPrerecordStart = tmp;
					} while(tmp != m_rLatestKeyFrame);
				}
				m_rLatestKeyFrame = index;
			}

			BYTE *pNext = pNode->pData + pNode->len;
			if(pNext + m_dwMaxFrameSize >= m_pBuffer + m_dwBufferSize) 
				pNext = m_pBuffer;

			m_pNewestNode = pNode;
			m_wIdx = (m_wIdx + 1) % m_dwCount;
			m_pNodes[m_wIdx].pData = pNext;
			m_pNodes[m_wIdx].len = 0;
			m_pNodes[m_wIdx].nalType = m_pNodes[m_wIdx].isKeyFrame = m_pNodes[m_wIdx].ready = 0;

#ifdef _DEBUG
			//TRACE("QueueFrame: %d\n", index);
#endif
		}
	}
}

//Break data pointed by pNode into two frames, return the second frame
FRAMENODE* CReceiverBuffer::BreakFrame(FRAMENODE *pNode, UINT newlen)
{
	if(!pNode) return NULL;
	int index = (pNode - m_pNodes);
	if(index == m_wIdx && newlen < pNode->len)
	{
		if(pNode->isKeyFrame)
		{
			if(pNode->timeStamp - m_pNodes[m_rPrerecordStart].timeStamp > 5000)
			{
				DWORD tmp = m_rPrerecordStart;
				do {
					tmp = (tmp + 1) % m_dwCount;
					if( (m_pNodes[tmp].isKeyFrame) && (pNode->timeStamp - m_pNodes[tmp].timeStamp > 5000) )
						m_rPrerecordStart = tmp;
				} while(tmp != m_rLatestKeyFrame);
			}
			m_rLatestKeyFrame = index;
		}

		BYTE *pNext = m_pNodes[index].pData + newlen;
		m_wIdx = (m_wIdx + 1) % m_dwCount;
		m_pNodes[m_wIdx].len = m_pNodes[index].len - newlen;
		m_pNodes[index].len = newlen;
		if(pNext + m_dwMaxFrameSize >= m_pBuffer + m_dwBufferSize)
		{
			m_pNodes[m_wIdx].pData = m_pBuffer;
			memcpy(m_pBuffer, pNext, m_pNodes[m_wIdx].len);
			ReserveSpace(m_pNodes[m_wIdx].len);
		}
		else
			m_pNodes[m_wIdx].pData = pNext;
	}
	else
		QueueFrame(pNode);

	m_pNodes[m_wIdx].isKeyFrame = m_pNodes[m_wIdx].ready = 0;
	return &m_pNodes[m_wIdx];
}

FRAMENODE *CReceiverBuffer::GetDataFrame()
{
	return (m_rIdx == m_wIdx)?NULL:&m_pNodes[m_rIdx];
}

void CReceiverBuffer::DequeueFrame(FRAMENODE *pNode)
{
	if(pNode)
	{
		int index = (pNode - m_pNodes);
		if(index == m_rIdx)
		{
			m_rIdx = (m_rIdx + 1) % m_dwCount;
		}
	}
}

void CReceiverBuffer::ClearBuffer()
{
	m_rIdx = m_wIdx = m_rPrerecordStart = m_rLatestKeyFrame = 0;
	if(m_pNodes)
	{
		memset(m_pNodes, 0, sizeof(FRAMENODE));
		m_pNodes[0].pData = m_pBuffer;
	}
}

FRAMENODE *CReceiverBuffer::GetPrerecordStartFrame()
{
	if(m_rPrerecordStart == m_wIdx) return NULL;
	return &m_pNodes[m_rPrerecordStart];
}
FRAMENODE *CReceiverBuffer::GetNewestFrame()
{
	return m_pNewestNode;
}

void CReceiverBuffer::ReadFromLatestKeyFrame()
{
	m_rIdx = m_rLatestKeyFrame;
}

UINT CReceiverBuffer::GetBufferedFrameCount()
{
	return (m_wIdx + m_dwCount - m_rIdx) % m_dwCount;
}
