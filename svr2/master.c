//return: id of location
int loc_of_ip(unsigned int ip);

int loc_to_server(int loc);

typedef struct { char server[48]; } SERVERADDR;


int find_p2p_servers(unsigned int ip, SERVERADDR candi[2], int *pN)
{
	int loc = loc_of_ip(ip);
}

