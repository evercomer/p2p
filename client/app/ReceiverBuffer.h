#ifndef __ReceiverBuffer_h__
#define __ReceiverBuffer_h__

#include "basetype.h"
///////////////////////////////////////////////////////////////////////////////////////
typedef struct _tagFRAMENODE {
	BYTE  *pData;
	DWORD time, timeStamp, len, flags;
	BYTE  nalType, isKeyFrame, ready;
} FRAMENODE;

class EXTERN CReceiverBuffer {
public:
	CReceiverBuffer(UINT maxFrameSize, UINT nBuffer, UINT nNode=1000);
	CReceiverBuffer();
	~CReceiverBuffer();
	void AllocateBuffer(UINT maxFrameSize, UINT nBuffer, UINT nNode=1000);
	void FreeBuffer();
	void ClearBuffer();

	FRAMENODE *GetFrameToWrite();		//Get the empty frame at end of queue (to write)
	BOOL ReserveSpace(UINT len);				//Call PreserveSpace before write data to frame.
	void QueueFrame(FRAMENODE *frame);			//Queue the data from just writed at the end. (Call QueueFrame when data written)
	FRAMENODE* BreakFrame(FRAMENODE *f, UINT newlen); //Return pointer to new frame

	FRAMENODE *GetDataFrame();			//Get data frame from the head of queue.
	void DequeueFrame(FRAMENODE *frame);//Remove frame from the head of queue
	
	FRAMENODE *GetPrerecordStartFrame();
	FRAMENODE *GetNewestFrame();
	FRAMENODE *NextFrame(FRAMENODE *pFrame);
	void ReadFromLatestKeyFrame();
	UINT GetBufferedFrameCount();
	inline BOOL	IsFull() { return m_bFull; };

	//CCriticalSection m_Lock;
private:
	FRAMENODE	*m_pNodes, *m_pNewestNode;
	DWORD		m_dwCount;
	DWORD		m_rPrerecordStart, m_rLatestKeyFrame;
	DWORD		m_rIdx, m_wIdx;

	BYTE	*m_pBuffer;
	DWORD	m_dwBufferSize, m_dwMaxFrameSize;
	BOOL	m_bFull;
};

#endif
