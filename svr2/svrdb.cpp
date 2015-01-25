#include "svrdb.h"
#include <time.h>
#include "md5.h"
#include <mysql.h>
#include "netbase.h"
#ifdef WIN32
#include <objbase.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <uuid/uuid.h>
#endif
#include "p2plog.h"


std::map<dcs_obj_key, dcs_ipcam*> ipcam_map;

static char _dbname[20], _dbuser[20], _dbpswd[20];

char *strncpyz(char *dst, const char *src, int n)
{
	if(src)
	{
		int i;
		for(i=0; i<n-1 && src[i]; i++)
			dst[i] = src[i];
		dst[i] = '\0';
	}
	return dst;
}

void gen_unique_id(uint8_t c[LENGTH_OF_SESSION_ID])
{
#ifdef WIN32

#if LENGTH_OF_SESSION_ID == 16
	CoCreateGuid((GUID*)c);
#else
	GUID guid;
	CoCreateGuid(&guid);
	memcpy(c, &guid, LENGTH_OF_SESSION_ID);
#endif

#else

#if LENGTH_OF_SESSION_ID == 16	//sizeof(uuit_t)
	uuid_generate(c);
#else
	uuid_t u;
	uuid_generate(u);
	memcpy(c, u, length_of_session_id);
#endif

#endif
}

time_t datestr2time(const char* s)
{
	char y[5], m[3], d[3];
	sscanf(s, "%[^-]-%[^-]-%2s", y, m, d);
	struct tm t;
	memset(&t, 0, sizeof(t));
	t.tm_year = atoi(y) - 1900;
	t.tm_mon = atoi(m) - 1;
	t.tm_mday = atoi(d);
	return mktime(&t);
}
char* date2str(time_t t, char str[11])
{
#ifdef WIN32
	struct tm *tt = localtime(&t);
	sprintf(str, "%4d-%02d-%02d", tt->tm_year+1900, tt->tm_mon+1, tt->tm_mday);
#else
	struct tm tt;
	localtime_r(&t, &tt);
	sprintf(str, "%4d-%02d-%02d", tt.tm_year+1900, tt.tm_mon+1, tt.tm_mday);
#endif
	return str;
}
//time_t -> YY(7)-MM(4)-DD(5) [yyyyyyymmmmddddd]
uint16_t time2packeddate(time_t t)
{
#ifdef WIN32
	struct tm *_tm;
	_tm = localtime(&t);
	return ((_tm->tm_year-100) << 9) | ((_tm->tm_mon+1)<<5) | _tm->tm_mday;
#else
	struct tm _tm;
	localtime_r(&t, &_tm);
	return ((_tm.tm_year-100) << 9) | ((_tm.tm_mon+1)<<5) | _tm.tm_mday;
#endif
}

//Input and output must has the format: yyyy-mm-dd
void inc_one_year(const char* date, char* result)
{
	int y = atoi(date);
	sprintf(result, "%d%s", y+1, date+4);	
	dbg_msg("inc_one_year: %s ==> %s\n", date, result);
}

//==========================================================================================================

int init_db(const char* dbname, const char*user, const char* pswd)
{
	strncpyz(_dbname, dbname, sizeof(_dbname));
	strncpyz(_dbuser, user, sizeof(_dbuser));
	strncpyz(_dbpswd, pswd, sizeof(_dbpswd));
	if(mysql_library_init(0, NULL, NULL))
	{
		fprintf(stderr, "Could not init mysql library.\n");
		exit(-1);
	}
	return 0;
}
void uninit_db()
{
	mysql_library_end();
}

MYSQL *open_db()
{
	MYSQL* mydb = mysql_init(NULL);
	if(!mydb)
	{
		fprintf(stderr, "Can't initialize MYSQL.\n");
		return NULL;
	}

	if(mysql_real_connect(mydb, NULL, _dbuser, _dbpswd[0]?_dbpswd:NULL, _dbname, 0, NULL, 0) == NULL)
	{
		fprintf(stderr, "%s\n", mysql_error(mydb));
		mysql_close(mydb);
		exit(0);
		return NULL;
	}
	return mydb;
}
inline void close_db(MYSQL* db)
{
	if(db) mysql_close(db);
}

MYSQL_RES* do_query(MYSQL* mydb, const char* query)
{
	if(mysql_query(mydb, query) == 0)
		return mysql_store_result(mydb);
	fprintf(stderr, "%s\n", mysql_error(mydb));
	return NULL;
}
BOOL exec_query(MYSQL* mydb, const char* query)
{
	if(mysql_query(mydb, query) == 0) 
		return TRUE;
	fprintf(stderr, "%s\n", mysql_error(mydb));
	return FALSE;
}

BOOL verify_account(const char* user, const uint8_t challenge[16], const uint8_t auth[LENGTH_OF_AUTH])
{
	char to[50], query[200];
	MYSQL *mydb;
	MYSQL_RES* res;

	mydb = open_db();
	if(!mydb) return FALSE;

	//mysql_hex_string(to, user, strlen(user));
	mysql_real_escape_string(mydb, to, user, strlen(user));
	sprintf(query, "select pswd from account where user='%s'", to);
	res = do_query(mydb, query);
	if(!res) 
	{
		dbg_msg("user %s not existed.\n", user);
		close_db(mydb);
		return FALSE;
	}

	BOOL rlt = FALSE;
	uint8_t tmp[32];
	MYSQL_ROW row;
	memset(tmp, 0, 32);
	if(row = mysql_fetch_row(res))
	{
		MD_CTX ctx;
		uint8_t _auth[LENGTH_OF_AUTH];
		strncpy((char*)tmp, row[0], 16);
		memcpy(tmp + 16, challenge, 16);
		MDInit(&ctx);
		MDUpdate(&ctx, tmp, 32);
		MDFinal(_auth, &ctx);
		if(memcmp(_auth, auth, LENGTH_OF_AUTH) == 0)
			rlt = TRUE;
	}
	mysql_free_result(res);
	close_db(mydb);
	return rlt;
}

const char encrypt_key[] = "KillBill#@#b$3X";
BOOL verify_ipcam(const char *sn, const uint8_t key[LENGTH_OF_AUTH])
{
#if 0
	MD5_CTX ctx;
	uint8_t tmp[16];
	int i;

	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char*)sn, 16);
	MD5Update(&ctx, (unsigned char*)encrypt_key, sizeof(encrypt_key));
	MD5Final(tmp, &ctx);
	for(i=0; i<4; i++) tmp[i] ^= tmp[i+12];
	return memcmp(tmp, key, 12) == 0;	
#else
	/* 
	 * The key is not bound to a S/N, we lost 
	 * the ability to check the ipcam's validity.
	 */
	return TRUE;
#endif
}

static BOOL is_key_valid(const char* key)
{
	return TRUE;
}

static uint8_t ac_key[32];
const char _vkey[] = "OkfnwSDRFS!@vuL^k32W#598h:^Sxd%k2*";
void LoadActivationKey(unsigned int index)
{
	int fd;
#ifdef WIN32
	char path[MAX_PATH], *slash;
	GetModuleFileName(NULL, path, MAX_PATH);
	slash = strrchr(path, '\\');
	strcpy(slash+1, "activate.key");
	fd = open(path, O_RDONLY);
#else
	fd = open("/etc/tasp2p/activate.key", O_RDONLY);
#endif
	if(fd < 0)
		memset(ac_key, 0xEF, 32);
	else
	{
		struct stat _stat;
		uint32_t max_idx;
		unsigned char digest[16];
		MD_CTX ctx;

		fstat(fd, &_stat);
		max_idx = _stat.st_size/32;
		if(index >= max_idx) index = max_idx - 1;
		lseek(fd, SEEK_SET, 32*index);
		read(fd, ac_key, 32);
		close(fd);

		MD5Init(&ctx);

		MD5Update(&ctx, (unsigned char*)_vkey, strlen(_vkey));
		MD5Update(&ctx, (unsigned char*)ac_key, 16);
		MD5Final(digest, &ctx);
		if(memcmp(digest, ac_key+16, 16))
		{
			fprintf(stderr, "Error key");
			exit(0);
		}
		dbg_msg("load key from index %d\n", index);
	}
}
static void base32(uint8_t bin[10], char ac[16])
{
	const char _base32[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int i;
	for(i=0; i<2; i++)
	{
		ac[8*i]   = _base32[(bin[5*i]>>3) & 0x1F];
		ac[8*i+1] = _base32[((bin[5*i]<<2) | (bin[5*i+1]>>6)) & 0x1F];
		ac[8*i+2] = _base32[(bin[5*i+1] >> 1) & 0x1F];
		ac[8*i+3] = _base32[((bin[5*i+1]<<4) | (bin[5*i+2]>>4)) & 0x1F];
		ac[8*i+4] = _base32[((bin[5*i+2]<<4) | (bin[5*i+3]>>7)) & 0x1F];
		ac[8*i+5] = _base32[(bin[5*i+3]>>2) & 0x1F];
		ac[8*i+6] = _base32[((bin[5*i+3]<<3) | (bin[5*i+4]>>5)) & 0x1F];
		ac[8*i+7] = _base32[bin[5*i+4] & 0x1F];
	}
}

BOOL verify_activate_code(const char *sn, const char *activate_code)
{
	MD_CTX ctx;
	uint8_t md5[16];

	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char*)sn, strlen(sn));
	MD5Update(&ctx, ac_key, 32);
	MD5Final(md5, &ctx);

	int i;
	char ac[16];
	for(i=0; i<6; i++) md5[i] ^= md5[10+i];
	base32(md5, ac);
	return memcmp(ac, activate_code, 16) == 0;
}

BOOL charge_for_ipcam(const char *sn, const char* key, const char* name, char* valid_date)
{
	if(!is_key_valid(key)) return FALSE;

	MYSQL *mysql = open_db();
	if(!mysql) return FALSE;

	BOOL bRet = FALSE;
	char query[200];
	sprintf(query, "select expire from ipcam where sn='%s'", sn);
	MYSQL_RES* res = do_query(mysql, query);
	if(res)
	{
		char date[12];
		date2str(time(NULL), date);
		if(mysql_num_rows(res))
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if(strcmp(date, row[0]) > 0)	//expired
			{
				//sprintf(query, "update ipcam set name='%s', expire=DATE_ADD(CURDATE(), INTERVAL 1 YEAR) where sn='%s'", name, sn);
				//inc_one_year(date, valid_date);
			}
			else
			{
				//sprintf(query, "update ipcam set name='%s', expire=DATE_ADD(expire, INTERVAL 1 YEAR) where sn='%s'", name, sn);
				//inc_one_year(row[0], valid_date);
			}
			exec_query(mysql, query);
		}
		else
		{
			sprintf(query, "insert into ipcam values(0, '%s', '%s', CURDATE(), DATE_ADD(CURDATE(), INTERVAL 1 YEAR))", sn, name);
			inc_one_year(date, valid_date);
			exec_query(mysql, query);
		}
		mysql_free_result(res);
		bRet = TRUE;
	}
	close_db(mysql);

	return bRet;
}

BOOL register_ipcam(const char *sn, const char* key, char *valid_date)
{
	MYSQL *mysql = open_db();
	if(!mysql) return FALSE;

	BOOL bRet = FALSE;
	char query[200];
	sprintf(query, "select expire from ipcam where sn='%s'", sn);
	MYSQL_RES* res = do_query(mysql, query);
	if(res)
	{
		char date[12];
		date2str(time(NULL), date);
		if(verify_activate_code(sn, key))
		{
			if(!mysql_num_rows(res))
			{
				sprintf(query, "insert into ipcam values(0, 0, '%s', '', CURDATE(), DATE_ADD(CURDATE(), INTERVAL 1 YEAR))", sn);
				if( (bRet = exec_query(mysql, query)) )
					inc_one_year(date, valid_date);
			}
			else
			{
				*valid_date = '\0';
				bRet = TRUE;
			}
		}

		mysql_free_result(res);
	}
	close_db(mysql);

	return bRet;
}

BOOL user_existed(const char* user)
{
	MYSQL *mydb = open_db();
	if(!mydb) return FALSE;

	char query[200], to[50];
	BOOL b = FALSE;
	mysql_real_escape_string(mydb, to, user, strlen(user));
	sprintf(query, "select user from account where user='%s'", to);

	MYSQL_RES* res = do_query(mydb, query);
	if(res)
	{
		b = mysql_num_rows(res) > 0;
		mysql_free_result(res);
	}
	close_db(mydb);
	return b;
}

BOOL ipcam_existed(const char* sn)
{
	MYSQL *mydb = open_db();
	if(!mydb) return FALSE;

	BOOL b = FALSE;
	char query[100];
	sprintf(query, "select sn from ipcam where sn='%s'", sn);
	MYSQL_RES* res = do_query(mydb, query);
	if(res)
	{
		b = mysql_num_rows(res) > 0;
		mysql_free_result(res);
	}
	close_db(mydb);
	return b;
}

BOOL insert_ipcam(const char* user, const char *sn, const char* name)
{
	MYSQL *mydb = open_db();
	if(!mydb) return FALSE;

	char query[200];
	sprintf(query, "insert into ipcam values(select uid from account where user='%s', '%s', '%s')", user, sn, name);
	BOOL b = exec_query(mydb, query);
	close_db(mydb);
	return b;
}

//Return:
//	0 - OK
//	1 - Not existed
//	2 - Inactive
int ipcam_state(const char* sn)
{
	return 0;
}

BOOL load_ipcam_info(const char* sn, struct dcs_ipcam *pcam)
{
	strcpy(pcam->sn, sn);

	MYSQL *mydb = open_db();
	if(!mydb) return FALSE;

	char query[200];
	BOOL bRet = FALSE;
	sprintf(query, "select gid, name, expire from ipcam where sn='%s'", sn);
	MYSQL_RES* res = do_query(mydb, query);
	if(res)
	{
		if(mysql_num_rows(res))
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			pcam->gid = atoi(row[0]);
			pcam->name[0] = '\t';
			strncpyz(pcam->name+1, row[1], sizeof(pcam->name)-1);
			pcam->no_name_in_db = pcam->name[0]?0:1;
			pcam->expire = datestr2time(row[2]);
			Log("load_ipcam_info", "ipcam info: expire = %u\n", pcam->expire);
			pcam->flags = 0;
			bRet = TRUE;
		}
		mysql_free_result(res);
	}
	close_db(mydb);
	return bRet;
}

#if 0
//S->C Ipcam record returned by server to client
struct dcs_cam_record {
	uint32_t gid;	//group id

	char sn[LENGTH_OF_SN];

	uint8_t reserved:6;
	uint8_t online:1;
	uint8_t active:1;
	uint8_t reserved2;
	uint16_t packed_expire_date;	//yy(7)-mm(4)-dd(5)
	struct nat_info stun;
	char name[LENGTH_OF_NAME];
	uint16_t version;
};

//S->C Group information
struct dcs_cam_group {
	uint32_t gid;
	char name[LENGTH_OF_NAME];
};

//S->C: Response of login
struct dcs_dev_list {
	struct dcs_header dh;
	uint32_t n_grp, n_cam;
	//struct dcs_dev_group ddg[0];
	//...
	//struct dcs_cam_record ddr[0];
	//...
};

int load_ipcams(const char* user, struct dcs_cam_group** ppg, uint32_t* n_grp, struct dcs_cam_record **ppi, uint32_t *n_cam)
{
	*ppi = NULL;
	*ppg = NULL;
	*n_grp = 0;
	*n_cam = 0;

	MYSQL *mydb = open_db();
	if(!mydb) return 0;

	char query[200];
	sprintf(query, "select uid from account where user='%s'", user);
	MYSQL_RES* res = do_query(mydb, query);
	if(res && mysql_num_rows(res))
	{
		MYSQL_ROW row;
		unsigned int uid, i;

		row = mysql_fetch_row(res);
		uid = atoi(row[0]);
		mysql_free_result(res);

		sprintf(query, "select gid, name from camgrp where uid=%d", uid);
		res = do_query(mydb, query);
		if(res)
		{
			*n_grp = mysql_num_rows(res);
			if(*n_grp)
			{
				struct dcs_cam_group *pgrp = (struct dcs_cam_group*)calloc(sizeof(struct dcs_cam_group), *n_grp);
				i = 0;
				while((row = mysql_fetch_row(res)))
				{
					pgrp[i].gid = atoi(row[0]);
					strncpyz(pgrp[i].name, row[1], sizeof(pgrp[i].name));
					//dbg_msg("group %d: %s\n", pgrp[i].gid, pgrp[i].name);
					i++;
				}
				*ppg = pgrp;
			}
			mysql_free_result(res);
		}


		sprintf(query, "select gid, sn, name, expire from ipcam where uid=%d", uid);
		res = do_query(mydb, query);
		if(res)
		{
			*n_cam = mysql_num_rows(res);
			if(*n_cam)
			{
				unsigned int now = time(NULL);
				struct dcs_cam_record *pi = (struct dcs_cam_record*)calloc(sizeof(struct dcs_cam_record), *n_cam);
				i = 0;
				while((row = mysql_fetch_row(res)))
				{
					pi[i].gid = atoi(row[0]);
					strncpy(pi[i].sn, row[1], LENGTH_OF_SN);
					strncpyz(pi[i].name, row[2], sizeof(pi[i].name));
					pi[i].active = (datestr2time(row[3]) > now)?1:0;
					//dbg_msg("ipcam : %s\n", pi[i].name);

					i++;
				}
				*ppi = pi;
			}
			mysql_free_result(res);
		}

	}
	else if(res) mysql_free_result(res);

	close_db(mydb);

	return 0;
}
#endif

struct dcs_ipcam* get_ipcam(const char *sn)
{
	IPCAMMAP::iterator ii = ipcam_map.find(sn);
	if(ii == ipcam_map.end()) return NULL;
	return ii->second;
}

//Create and load ipcam from database
struct dcs_ipcam* load_ipcam(const char *sn)
{
	struct dcs_ipcam* pipc = NULL;

	pipc = new dcs_ipcam();
	if(load_ipcam_info(sn, pipc))
	{
		ipcam_map.insert(std::make_pair(sn, pipc));
		return pipc;
	}
	else
	{
		delete pipc;
		return NULL;
	}
}
