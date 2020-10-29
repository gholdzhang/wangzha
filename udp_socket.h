#ifndef __UDP_SOCKET_H__
#define __UDP_SOCKET_H__
#include <map>
#include <pthread.h>
#include "threadpool.h"
#include "sgg_bitmap.h"
#include "sgg_stack.h"
#include "sgg_manage.h"

#define UDP_SEND_TASK_MAX_NUM         50
#define UDP_SEND_TASK_MIN_NUM         5
#define UDP_RECV_TASK_MAX_NUM         50

//#define UDP_SLIDE_WIN_MAX 100

enum {
    UDP_ACK = 0,
    UDP_RT_SPECIAL = 1,
    UDP_RT_ALL = 2
};

typedef struct {
    unsigned int pktFlag;
    unsigned int pktOffset;
}pktFlagWithOffset;

typedef struct {
    unsigned int magix;
    unsigned int msgType;
    unsigned int msgLen;
    unsigned int version;
    unsigned int errCode;
}VerifyMsgHead;

typedef struct {
    unsigned int pktFlag;
    unsigned int totalLen;
    unsigned int rcvedLen;    
    unsigned int usedFlag; /* 0:uninit, 1:unused, 2:used*/
    unsigned int waiting;
    unsigned int waitCloseStatus;
    unsigned int endOffset;
    unsigned int reTrans;/* 重传次数 */
    unsigned int recvFromIp;
    unsigned int recvFromPort;
    long timeStamp; //时间戳，单位毫秒
    bits bit;
    std::map<int,int> lenMap;
}PackQueueHead;

typedef struct {
    PackQueueHead head;
    char *buf;
}PackQueueInfo;

typedef struct {
    PSTACK PktBufStack[UDP_RECV_QUEUE_SIZE];
    int wpos; //写线程当前操作的队列；
    int rpos; //读线程当前操作的队列
    pthread_mutex_t lock;
    pthread_cond_t  cond;
}UDPRecvDataQueue;

typedef struct {
    std::map<Pkt3Tuple,pktFlagWithOffset> pktFlagmap;
    std::map<unsigned int,int> pid2sidmap;
    pthread_mutex_t mutex;
}udpPktFlagMaps;

typedef struct {
    int location;
    int listen_sid;
    int shutdown;
    unsigned int sgg_multi_lines; 
    int remote_socket_type;
    unsigned int remote_ip;
    unsigned int remote_port;
    unsigned int udp_send_interval;
    SocketInfo* socketIds;
    PackQueueInfo *pktQueue;
    UDPRecvDataQueue *pUDPRecvDataQueues;
    udpPktFlagMaps *pktFlagMaps;
    int datalen;
    char *pbuf;
}udp_thread_send_para;

typedef struct {
    int listen_sid;
    int location;
    int shutdown;
    unsigned int sgg_multi_lines; 
    unsigned int listen_ip;
    unsigned int listen_port;
    char rtpCfgFlag[4];
    UDPRecvDataQueue *pUDPRecvDataQueues;
}udp_thread_recv_para;




int run_udpclient(int ipaddr, int port, int sendInterval);
int send_udpdata(int sid, char*buf, int dataLen, unsigned int send_interval, bool cacheFlag);
void close_udpclient(int sid);
int sgg_udp_server_init(void);
bool checkAllUdpPktsAcked(int sid);
int GetUdpPktHeartCounter(int sd);
void ResetUdpPktHeartCounter(int sd);


//void *addUDPTask(void *arg1, void *arg2, void*arg3);


typedef struct _CacheMsgSlideWin {
    int sockId;
    unsigned int pktFlag;
    unsigned int pktOffset;
    unsigned int pktAcked;
    unsigned int idleTimes;
    unsigned int existHeart; /* 0表示不存在心跳，>1表示存在心跳 */ 
    PSTACK pktBufStack;/* 缓存队列 */
    pthread_t thrdId;
    pthread_rwlock_t rwlock;
    pthread_mutex_t mutex;
    struct _CacheMsgSlideWin & operator=(const struct _CacheMsgSlideWin &obj1) {
        this->sockId = obj1.sockId;
        return *this;
    }
}CacheMsgSlideWin;
class UdpFlowControl {
public:
    UdpFlowControl();
    ~UdpFlowControl();
    int add(int sd);
    int rmv(int sd);
    int push(int sd, char*data, unsigned int dataLen);
    int pop(int sd, int len);
    int resend(int sd, unsigned int offset);
    int resendAll(int sd, unsigned int pktFlag);
    int ack(int sd, unsigned int offset);
    bool isAllAcked(int sd);
    void resetHeartCounter(int sd);
    int getHeartCounter(int sd);
    static UdpFlowControl& GetInstance();
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    CacheMsgSlideWin** ppFlow;
private:
    pthread_t thrdId;    
};

#define UdpFlowCtlIns UdpFlowControl::GetInstance()


class udp_server_obj {
    public:
        udp_server_obj(ServerCfgInfo &cfg);
        udp_server_obj(const udp_server_obj      &obj);
        ~udp_server_obj();
        int init();
        int run();
        int getshutdown(void);
        void setshutdown(int flag);
        void check_clientsocket(unsigned int sgg_multi_lines, unsigned int pos, unsigned int location);
        int request_queueDropedPkt(unsigned int pktFlag);
        
    private:        
        pthread_t serverpktrecvthrd;
        pthread_t serverpktsendthrd;
        udp_thread_send_para *pthreadpara;
        udp_thread_recv_para *pthdrecvpara;
        SocketInfo* socketIds;
        PackQueueInfo *pktQueue;
        UDPRecvDataQueue UDPRecvDataQueues;
        int socket_id;        
        
        pthread_rwlock_t rwlock;
        int shutdown;   /* true为关闭 */
        
    public:    
        pthread_t thrId;
        int runFlag;
        ServerCfgInfo confInfo;
        udpPktFlagMaps pktFlagMaps;
        
};

class udp_server_manage{
    public:
        udp_server_manage(void*arg);
        udp_server_manage();
        ~udp_server_manage();
        int run();
        int init(void*arg);
        int rmv(SvrObjBaseInfo*cfg);
        int rmv(SvrObjBaseInfo*cfg, int num);
        int add(SvrObjCommInfo *commCfg, SvrObjBaseInfo*cfg);
        static udp_server_manage* GetInstance();
    private:
        SggConfInfo confInfo;
        pthread_rwlock_t rwlock;
        std::map<int,udp_server_obj> servers;
};

/*首次调用该接口一定要调用init进行初始化*/
#define UdpSvrManageInstance udp_server_manage::GetInstance()


#endif

