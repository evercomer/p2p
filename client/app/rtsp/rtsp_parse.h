#ifndef __HI_RTSP_PARSE_H__
#define __HI_RTSP_PARSE_H__

#ifdef __cplusplus
extern "C"{
#endif /* __cplusplus */


#define RTSP_DEFAULT_SVR_PORT	554

//#define HDRBUFLEN   4096
//#define TOKENLEN    100


#define HI_RTSP_PARSE_OK 0
#define HI_RTSP_PARSE_IS_RESP  -4
#define HI_RTSP_PARSE_INVALID_OPCODE -1
#define HI_RTSP_PARSE_INVALID - 2
#define HI_RTSP_PARSE_ISNOT_RESP -3

char *RTSP_Get_Status_Str( int code );

//rtsp://user@pswd:host:port/serial/media
int RTSP_Parse_Url(const char *url, char *server, int *port, char *file_name, char *user, char *password);


#ifdef __cplusplus
}
#endif

#endif

