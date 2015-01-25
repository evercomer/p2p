#ifndef __svrdb_h__
#define __svrdb_h__

#include "p2pbase.h"
#include "linux_list.h"
#include <string.h>
#include <map>
#include <vector>


struct dcs_obj_key {
	dcs_obj_key() { memset(key, 0, sizeof(key)); }
	dcs_obj_key(const char* k) { strcpy(key, k); }

	char key[LENGTH_OF_SN];
};
inline BOOL operator < (const dcs_obj_key& o1, const dcs_obj_key& o2)
{
	return strcmp(o1.key, o2.key) < 0;
}

struct dcs_ipcam {
	struct list_head list;  //All-objects list. The oldest one is at first
	char sn[LENGTH_OF_SN];	//referenced by dcs_client

	uint32_t gid;	//group id

	unsigned int last_access;
	uint32_t trans_id;
	time_t expire;
	uint8_t no_name_in_db:1;		//name not set in db, updated by ipcam's report
/*
	uint8_t need_ac:1;
	uint8_t sdc:1;
	uint8_t nas:1;
*/
	uint8_t flags;
	char name[48];
	uint32_t version;			//Version of firmware, network byte order

	int sock;			//bound udp socket which receive ipcam's request, send 
	struct p2pcore_addr ext_addr;	//response or session request on this socket later

	struct nat_info stun, stunt;

	dcs_ipcam() { expire = 0; sock = 0; flags = 0; INIT_LIST_HEAD(&list); }
};
struct dcs_client {
	//uint8_t ac[16];
	char user[20];
	unsigned int cseq;

	unsigned int last_access;

	std::vector<dcs_ipcam*> vipc;

	dcs_client() { user[0] = '\0'; }
};

typedef std::map<dcs_obj_key, dcs_ipcam*> IPCAMMAP;
extern IPCAMMAP ipcam_map;

void gen_unique_id(uint8_t c[LENGTH_OF_SESSION_ID]);

int init_db(const char* db, const char* user, const char* pswd);
void uninit_db();

BOOL verify_account(const char* user, const uint8_t challenge[16], const uint8_t auth[LENGTH_OF_AUTH]);
BOOL verify_ipcam(const char *sn, const uint8_t key[LENGTH_OF_AUTH]);

//int load_user(const char* user, ...);
BOOL user_existed(const char* user);

BOOL ipcam_existed(const char* sn);

//
BOOL load_ipcam_info(const char* sn, struct dcs_ipcam *pcam);

//Return TRUE if successful, valid_date is the date until the service is invalid
//	FALSE means the key is invalid
BOOL charge_for_ipcam(const char *sn, const char* key, const char* name, char* valid_date);

///* Add an ipcam record to database if not existed.
//Return TRUE if successful or existed, valid_date is the date until the service is invalid
//	FALSE means the key is invalid
BOOL register_ipcam(const char *sn, const char* key, char *valid_date);

//Return:
//	0 - OK
//	1 - Not existed
//	2 - Inactive
int ipcam_state(const char* sn);

///Load ipcams from database
//Parameters:
//	user	-- user's name
//	ppi	-- address of a pointer to dcs_cam_record. this function allocate 
//		   memory to return all records. caller must call free() on it
//	n_grp	-- to return the number of groups
//	ppg	-- address of a pointer to dcs_cam_group. this function allocate
//		   memory to return all groups. caller must call free() on it
//	n_cam	-- to return the number of cameras
//Return: the count of records
//
#define IPCAMIF_ACTIVE	1
#define IPCAMIF_ONLINE	2
#define IPCAMIF_NEED_AC	4
int load_ipcams(const char* user, struct dcs_cam_group** ppg, uint32_t* n_grp, struct dcs_cam_record** ppi, uint32_t *n_cam);

//Create and load an ipcam object from database
struct dcs_ipcam* load_ipcam(const char* sn);

//Find the ipcam from map
struct dcs_ipcam* get_ipcam(const char *sn);

BOOL insert_ipcam(const char* user, char *sn, const char* name);

//s is "yyyy-mm-dd"
time_t datestr2time(const char* s);

//function: time_t to "yyyy-mm-dd"
//return:   str
char* date2str(time_t t, char str[11]);

//time_t -> YY(7)-MM(4)-DD(5) [yyyyyyymmmmddddd]
uint16_t time2packeddate(time_t);
//--------------------------------------

void LoadActivationKey(unsigned int index);

char *strncpyz(char *dst, const char *src, int n);

#endif	//#ifndef __svrdb_h__
