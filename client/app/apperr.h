#ifndef __apperr_h__
#define __apperr_h__

#define E_OK	0

#define E_INVALID_PARAM	201

/* Error from Read/Write system */
#define E_CANNOTCREATEFILE	202
#define E_CANNOTOPENFILE	203
#define E_BADFMT			204	//bad file format
#define E_BADVER			205
#define E_TAGNOTEXISTED		206	//has no such tag
#define E_BUFFERTOOSMALL	207	//size of buffer is less than size of frame
#define E_EOF				208	//end of file
#define E_WAITDATA			209	//RemoteReader is waiting for next frame

#define E_OTHER	210

#endif
