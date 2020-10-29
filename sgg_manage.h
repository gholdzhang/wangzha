#ifndef __SGG_MANAGE_H__
#define __SGG_MANAGE_H__

#include "sgg_stack.h"

#define PER_PACKET_MAX_SIZE (32*1024)
#define PKT_DATA_LEN  (PER_PACKET_MAX_SIZE-28) //接收报文最大长度减去自定义报文头部长度sizeof(UserHeadInfo)
#define MAGIC_WORD_MAIN 0xDEADBEEF
#define MAGIC_WORD_BANK 0xBEEFDEAD
#define MAGIC_WORD_TAIL 0xDEADABCD
#define MAGIC_WORD_DETECT 0xDEADBADA

#define UDP_PROTOCAL_BUFF_MAX_SIZE (500*1024*1024)
#define UDP_RECV_QUEUE_SIZE 2
#define TCP_RECV_QUEUE_MAX_COUNT 2
#define TCP_BUFF_STACK_MAX_COUNT  (128)
#define UDP_BUFF_STACK_MAX_COUNT  (1024)
#define SGG_MULTI_LINES_DEFAULT_NUM 50
#define UDP_TUNNEL_IDLE_TIMES 10 /* 单位秒 */
#define UDP_TUNNEL_IDLE_CRCLES 3 /* 次数 */




enum {
    SGG_MODE_0 = 0,
    SGG_MODE_1,
    SGG_MODE_BUTT    
};

typedef  enum  _PACKAGE_DIRECT
{
	PACKET_DIRECTION_UP = 0,
	PACKET_DIRECTION_DOWN,
	PACKET_DIRECTION_BUTT
}PACKAGE_DIRECT;
    

/* 
* 注意：如下数据结构新增字段后，需要同步修改PKT_DATA_LEN
*/
typedef struct
{
	unsigned int magicWord;
	unsigned int pktFlag; //包标记
	unsigned int pktTotalLen;
	unsigned int offset; //偏移量
	unsigned int buf_size; //本片报文长度 
	unsigned int recvFromIp;
    unsigned int recvFromPort;
}UserHeadInfo;

/* 接收包 */
typedef struct SendPack
{
	UserHeadInfo head;
	char buf[PKT_DATA_LEN];
} SendPackStr;

typedef struct {
    unsigned int location;//0:CLIENT,1:SERVER
    unsigned int udp_send_interval; //单位微秒
    unsigned int udp_send_hb; //是否发送udp 心跳包，0表示不发送，1表示发送，默认发送
    unsigned int listen_socket_type; //0:UDP,1:TCP
    unsigned int listen_ip;
    unsigned int listen_port;
    unsigned int remote_socket_type; //0:UDP,1:TCP
    unsigned int remote_ip;
    unsigned int remote_port;    
    unsigned int tcp_buff_max_count;
    unsigned int udp_buff_max_count;
    unsigned int sgg_multi_lines; 
    char rtpCfgFlag[4];
}ServerCfgInfo;


typedef struct {
    unsigned int location;//0:CLIENT,1:SERVER
    unsigned int udp_send_interval; //单位微秒
    unsigned int udp_send_hb; //是否发送udp 心跳包，0表示不发送，1表示发送，默认发送
    unsigned int listen_socket_type; //0:UDP,1:TCP
    unsigned int listen_ip;
    unsigned int listen_port;
    unsigned int listen_port_begin;
    unsigned int listen_port_end;
    unsigned int remote_socket_type; //0:UDP,1:TCP
    unsigned int remote_ip;
    unsigned int remote_port;
    unsigned int remote_port_begin;
    unsigned int remote_port_end;
    unsigned int dlisten_socket_type; //0:UDP,1:TCP
    unsigned int dlisten_ip;
    unsigned int dlisten_port;
    unsigned int dremote_socket_type; //0:UDP,1:TCP.下行方向远端socket模式，sgg_mode=1时有效
    unsigned int dremote_ip; //下行方向远端IP，sgg_mode=1时有效
    unsigned int dremote_port; //下行方向远端PORT，sgg_mode=1时有效
    unsigned int tcp_buff_max_count;
    unsigned int udp_buff_max_count;
    unsigned int sgg_multi_lines; 
    unsigned int sgg_mode;//0(默认):网闸或者光闸，1：Nginx近端代理, SGG_MODE_BUTT
    unsigned int sgg_comm_port; //通信端口
}SggConfInfo;

typedef struct {
    PSTACK PktBufStack[TCP_RECV_QUEUE_MAX_COUNT];
    int wpos; //写线程当前操作的队列；
    int rpos; //读线程当前操作的队列
    pthread_mutex_t lock;
    pthread_cond_t  cond;
}TCPRecvDataQueue;


typedef struct {
    int socket_type;
    int socket_id;
    unsigned int remote_ip;
    unsigned int remote_port;
    unsigned int pktFlag;
    unsigned int idleTimes; /*单位秒*/
    unsigned int idleCrcles; /*次数*/
    pthread_mutex_t lock;
}SocketInfo;

typedef struct {
    pthread_rwlock_t rwlock;
    std::map<unsigned int, int> mapInfo;
    HashBucket<int> *hbMapInfo;
}Pktid2Socketid_s;

typedef struct {
    unsigned int msgType;
    unsigned int msgLen;
    unsigned int userId;
    unsigned int usedFlag; /* SL_USEDFLG_BUTT*/
    unsigned int version;
    unsigned int errCode;
    unsigned int reverse[6];
}MsgQueueHead;

typedef struct {
    MsgQueueHead head;
    char buf[PER_PACKET_MAX_SIZE];
}MsgQueueInfo;


typedef struct _Pkt5Tuple{
    unsigned int srcIP;
    unsigned int srcPort;
    unsigned int dstIP;    
    unsigned int dstPort;
    unsigned int protocol;/* 0:UDP,1:TCP */
    struct _Pkt5Tuple & operator=(const struct _Pkt5Tuple &obj) {
        this->srcIP = obj.srcIP;
        this->srcPort = obj.srcPort;
        this->dstIP = obj.dstIP;
        this->dstPort = obj.dstPort;
        this->protocol = obj.protocol;
        return *this;
    }
    bool operator==(const struct _Pkt5Tuple &obj) const {
        return (this->srcIP==obj.srcIP && this->srcPort==obj.srcPort && 
            this->dstIP==obj.dstIP && this->dstPort==obj.dstPort &&this->protocol==obj.protocol)?true:false;
    }
    bool operator < (const struct _Pkt5Tuple &obj) const {
        if(this->srcIP<obj.srcIP) {
            return true;
        } else if (this->srcIP==obj.srcIP) {
            return this->srcPort<obj.srcPort;
        }
        return false;
    }
}Pkt5Tuple;

typedef struct _Pkt3Tuple{
    unsigned int ip;
    unsigned int port;
    unsigned int protocol;/* 0:UDP,1:TCP */
    struct _Pkt3Tuple & operator=(const struct _Pkt3Tuple &obj) {
        this->ip = obj.ip;
        this->port = obj.port;
        this->protocol = obj.protocol;
        return *this;
    }
    bool operator==(const struct _Pkt3Tuple &obj) const {
        return (this->ip==obj.ip && this->port==obj.port && this->protocol==obj.protocol)?true:false;
    }
    bool operator < (const struct _Pkt3Tuple &obj) const {
        if(this->ip<obj.ip) {
            return true;
        } else if (this->ip==obj.ip) {
            return this->port<obj.port;
        }
        return false;
    }
    
}Pkt3Tuple;

typedef struct {
    unsigned int location;//0:CLIENT,1:SERVER
    unsigned int udp_send_interval;     /* udp报文发送间隔，该参数只对udp类型通道起作用 */
    unsigned int udp_send_heart_beat;   /* udp报文心跳标记，该参数只对udp类型通道起作用，且客户端和服务端必须一致 */
    unsigned int tcp_buff_max_count;    /* tcp缓存队列个数，配置越大，占用的系统内存越大； */
    unsigned int udp_buff_max_count;    /* udp缓存队列个数，配置越大，占用的系统内存越大 */
    unsigned int sgg_multi_lines;       /* 每个通道最大支持的并发数据，并发数越多，占用的内存和CPU资源越大 */
    unsigned int reverse[2];            /* 保留字段，扩展使用 */
}SvrObjCommInfo;

typedef struct {
    //unsigned int location;//0:CLIENT,1:SERVER
    Pkt3Tuple local;
    Pkt3Tuple remote;
    char rtpCfgFlag[4];/* 0~7bit：0表示不需要进行RTP流统计，1表示需要进行RTP统计,2表示需要进行视频流编码格式校验，3表示需要进行RTP统计+视频流编码格式校验；
                          8~15bit：0表示不需要插白帧，1表示需要插白帧；
                          16~23bit：表示插入白帧的间隔，单位K，范围1~255K；
                          24~31bit：表示插入白帧的长度，单位字节，范围1~255字节，该参数对TCP类型通道无效 */
}SvrObjBaseInfo;

int sgg_admin_process(char *cfg_file);
int get_runflag(void) ;
void set_runflag(int f);
SggConfInfo* getSggConfInfo(void);
void destroyTcpRecvQueue(TCPRecvDataQueue &queue, const unsigned int count);
int createTcpRecvQueue(TCPRecvDataQueue &queue, const unsigned int count, const unsigned int size);
int insetSocketIdByPktId(unsigned int pktid, int sd);
int getSocketIdByPktId(unsigned int pktid);
void deleteSocketIdByPktId(unsigned int pktid);
int checkTcpClientSocketAlive(int net_type, int flag, int pktdir, int remote_ip, int remote_port, int& sd, int stopFlag=0);

#endif

