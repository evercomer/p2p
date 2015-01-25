#ifndef __p2psvc_h__
#define __p2psvc_h__

#include "platform_adpt.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PRIVATE_P2P
#define OTHER_SERVICES_PORT	9121
#else
#define OTHER_SERVICES_PORT	8121
#endif

int init_p2p_svc();
void run_p2p_svc(BOOL new_thread);

void signal_stop_service();

void clean_p2p_svc();

struct p2psvc_cb {
	//收到心跳包时调用
	//返回:  0 - 正常
	//	 DCSS_INACTIVE - 将设备转为失活状态（减低心跳频率）
	//	 DCSS_ADDRESS_CHANGED - 服务器地址改变, new_server为新地址
	int (*on_heart_beat)(const char *sn, char *new_server);

	//一分钟内没有收到心跳包, 设备状态转为不在线
	int (*on_heart_no_beat)(const char *sn); 

	//连接请求
	//返回: 0 - 继续
	//	X - 状态码 DCSS_xxx
	//参数: ct - 连接类型: 2-p2p; 3-relay
	int (*on_session_init)(const char *sn, int ct);
};
void p2psvc_set_cb(struct p2psvc_cb *cb);

#ifdef __cplusplus
}
#endif

#endif
