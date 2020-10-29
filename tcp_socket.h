#ifndef __TCP_SOCKET_H__
#define __TCP_SOCKET_H__
#include <map>
#include "threadpool.h"
#include "sgg_manage.h"

#define TCP_PROTOCAL_BUFF_MAX_SIZE (64*1024)
#define TCP_PROTOCAL_WBUFF_MAX_SIZE (1*1024*1024)

typedef struct {
    int usedflag;
    int server_sid;
    int client_sid;
    int client_closewaite;
    int location;    
    int flag; /* 申请的线程池组标记 */
    int stop_flag;
    int shutdown;
    unsigned char exitFlag[4];/*四个方向（上行读写、下行读写）0表示正在运行未退出，1表示已经退出 */
    pthread_mutex_t lock;
    
    int sgg_mode; //SGG_MODE_BUTT
    int remote_socket_type;
    unsigned int remote_ip;
    unsigned int remote_port;
    int dremote_socket_type;
    unsigned int dremote_ip;
    unsigned int dremote_port;
    unsigned int udp_send_interval;
    unsigned int pktid; /* 报文id */
    unsigned int pktdir[4];
    TCPRecvDataQueue queue[PACKET_DIRECTION_BUTT];
}tcp_thread_para;


class tcp_server_obj{
    public:
        tcp_server_obj(ServerCfgInfo &cfg);
        tcp_server_obj(const tcp_server_obj      &obj);
        ~tcp_server_obj();
        int init(void);
        int run(void);
        int getshutdown(void);
        void setshutdown(int flag);

    private:
        unsigned int pktid;         
        Pktid2Socketid_s pktid2SidInfo;
        int socket_id;
        tcp_thread_para *bufMap=NULL;
        threadpool_t *thrdPools = NULL;
        pthread_rwlock_t rwlock;
        int shutdownFlag;   /* true为关闭 */
    public:    
        pthread_t thrId;
        int runFlag; //0:表示线程初始状态，1：表示线程正在运行状态；2：表示线程退出状态
        ServerCfgInfo confInfo;
        
        
};

class tcp_server_manage {
    public:
        tcp_server_manage(void*arg);
        tcp_server_manage();
        ~tcp_server_manage();        
        int init(void*arg);
        int add(SvrObjCommInfo *commCfg, SvrObjBaseInfo*cfg);
        int rmv(SvrObjBaseInfo*cfg);
        int rmv(SvrObjBaseInfo*cfg, int num);
        int run();
        static tcp_server_manage* GetInstance();
    private:
        SggConfInfo confInfo;
        pthread_rwlock_t rwlock;
        std::map<int,tcp_server_obj> servers;
};

/*首次调用该接口一定要调用init进行初始化*/
#define TcpSvrManageInstance tcp_server_manage::GetInstance()

int run_tcpclient(int ipaddr, int port, int sendInterval, bool blockflag);
void close_tcpclient(int sid);
int send_tcpdata(int sid, char*buf, int dataLen);
int run_tcpserver(void*arg);
int accept_socket(int listen_st, bool blockflag);
void sigdismiss_tcpsocket(void);
int create_tcplisten(int ipaddr, int port, int rcvBufMaxSize, int blockFlag=1);



#endif

