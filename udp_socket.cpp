#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <map>
#include <set>
#include <queue>
#include <list>
#include "utils.h"
#include "udp_socket.h"
#include "tcp_socket.h"
#include "log.h"
#include "sgg_err.h"
#include "sgg_manage.h"
#include "sgg_stack.h"
#include "sgg_bitmap.h"
#include "sgg_audit.h"

extern int g_stopflag;
int g_udpHbFlag = 1;/* udp发报心跳机制，默认打开 */




#define UDP_DATA_OFFSET 4
#define UDP_FLOW_NUM_MAX 65536

pthread_t g_serverpktrecvthrd = 0;
pthread_t g_serverpktsendthrd = 0;
pthread_mutex_t udp_mutex = PTHREAD_MUTEX_INITIALIZER;
int sendto_udpdata(int sid, char*buf, 
                    int dataLen, unsigned int send_interval, 
                    unsigned int dstIp, unsigned int dstPort);



void destroyRecvQueue(UDPRecvDataQueue &UDPRecvDataQueues)
{
    int i;

    if(UDPRecvDataQueues.PktBufStack[0] != NULL) {
        pthread_cond_destroy(&(UDPRecvDataQueues.cond));
        //pthread_mutex_lock(&(g_UDPRecvDataQueues.lock)); // /*先锁住再销毁,但是发生死锁，原因未知*/
        pthread_mutex_destroy(&(UDPRecvDataQueues.lock));
    }
    
    for (i=0; i<UDP_RECV_QUEUE_SIZE; i++) {
        if (UDPRecvDataQueues.PktBufStack[i] != NULL) {
            DestroyStack(&(UDPRecvDataQueues.PktBufStack[i]));            
        }        
    }
    
    return;
}

int createRecvQueue(UDPRecvDataQueue &UDPRecvDataQueues, unsigned int count)
{
    int i;
    RET_IF(count==0, -1);
    for (i=0; i<UDP_RECV_QUEUE_SIZE; i++) {
        UDPRecvDataQueues.PktBufStack[i] = NULL;
        CreateStack(&(UDPRecvDataQueues.PktBufStack[i]), count, sizeof(SendPackStr));  
        if(UDPRecvDataQueues.PktBufStack[i] == NULL) {
            goto end;
        }       
    }

    if (pthread_cond_init(&(UDPRecvDataQueues.cond), NULL) != 0 || 
        pthread_mutex_init(&(UDPRecvDataQueues.lock), NULL) != 0) {
        goto end;
    }

    UDPRecvDataQueues.wpos = 0;
    UDPRecvDataQueues.rpos = 1;
    
    return 1;
end:
    destroyRecvQueue(UDPRecvDataQueues);
    return -1;
}

#define UDP_PACKET_ORDER_LEN_MAX  100
unsigned int g_serverRecvMaxLen = (PER_PACKET_MAX_SIZE*UDP_PACKET_ORDER_LEN_MAX);

#define RECV_PKT_MIN_QUEUE 6000
extern unsigned int g_multilines;


static int InitOnePackQueue(PackQueueInfo **ppktQueue)
{
    int i;
    int bmLen = g_serverRecvMaxLen/PER_PACKET_MAX_SIZE;
    bmLen +=((g_serverRecvMaxLen%PER_PACKET_MAX_SIZE)?1:0);
    if(*ppktQueue == NULL){
        *ppktQueue = (PackQueueInfo*)malloc(sizeof(PackQueueInfo)*RECV_PKT_MIN_QUEUE);
        if (*ppktQueue == NULL) {
            DEBUG_PRINT("Create packet reassemb queue failed:%s \n", strerror(errno));
            return -1;
        }
        memset(*ppktQueue, 0, sizeof(PackQueueInfo)*RECV_PKT_MIN_QUEUE);
    }
    
    for(i=0; i<RECV_PKT_MIN_QUEUE; i++)
    {        
        if((*ppktQueue)[i].buf == NULL)
        {
            (*ppktQueue)[i].buf = (char*)malloc(g_serverRecvMaxLen);
            if((*ppktQueue)[i].buf == NULL)
            {
                return -1;
            }
            memset((*ppktQueue)[i].buf, 0, g_serverRecvMaxLen);    

            (*ppktQueue)[i].head.pktFlag = 0;
            (*ppktQueue)[i].head.totalLen = 0;
            (*ppktQueue)[i].head.rcvedLen=0;
            (*ppktQueue)[i].head.usedFlag = 1;                  
            (*ppktQueue)[i].head.waiting = 0; 
            (*ppktQueue)[i].head.waitCloseStatus = 0; 
            (*ppktQueue)[i].head.bit = bit_new(bmLen);
            (*ppktQueue)[i].head.lenMap.clear();
            
            DEBUG_PRINT("InitOnePackQueue sucess:index=%d,bmlen=%d\r\n",i,bmLen);
            ELOG(L_INFO,"InitOnePackQueue sucess:index=%d,bmlen=%d",i,bmLen);
            return i;
        }       
    }
    return -1;    
}

static void DestroyPackQueue(PackQueueInfo **ppktQueue, unsigned int pos)
{
    int i;
    if(pos >= RECV_PKT_MIN_QUEUE)
    {
        for(i=0; i<RECV_PKT_MIN_QUEUE; i++)
        {
            if((*ppktQueue)[i].buf != NULL)
            {
                free((*ppktQueue)[i].buf);
                (*ppktQueue)[i].buf = NULL;
            }
            bit_destroy((*ppktQueue)[i].head.bit);

        }
        free(*ppktQueue);
        *ppktQueue = NULL;
    }
    else
    {
        if((*ppktQueue)[pos].buf != NULL)
        {
            free((*ppktQueue)[pos].buf);
            (*ppktQueue)[pos].buf = NULL;
        }
    }
    
    return;
}

int ReleaseOneOverTimeQueue(PackQueueInfo *pktQueue)
{
    int i;
    struct timeval tv;
    struct timezone tz;
    unsigned long timeDelta = 0;
    unsigned long timeDeltaMax = 0;
    int pos = 0;
    //int usedCount = 0;
    
    (void)gettimeofday(&tv, &tz);
    

    for(i=0; i<RECV_PKT_MIN_QUEUE; i++)
    {
        if(pktQueue[i].head.usedFlag == 2)
        {
            timeDelta = (unsigned long)((tv.tv_sec*1000 + tv.tv_usec/1000) - pktQueue[i].head.timeStamp);
            //printf("%u,%u,%u\r\n",(tv.tv_sec*1000 + tv.tv_usec/1000), g_pktQueue[i].head.timeStamp, timeDelta);
            if(timeDeltaMax < timeDelta)
            {
                timeDeltaMax = timeDelta;
                pos = i;
            }
            //usedCount++;
        }
        
    }

    DEBUG_PRINT("release one packet reassembly queue:timeDelta=0x%08x, pktflg=0x%08x, tlen=0x%08x,rlen=0x%08x\r\n",timeDeltaMax, 
    pktQueue[pos].head.pktFlag, 
    pktQueue[pos].head.totalLen,
    pktQueue[pos].head.rcvedLen);

    ELOG(L_INFO,"release one packet reassembly queue:timeDelta=0x%08x, pktflg=0x%08x, tlen=0x%08x,rlen=0x%08x", 
    timeDeltaMax, 
    pktQueue[pos].head.pktFlag, 
    pktQueue[pos].head.totalLen,
    pktQueue[pos].head.rcvedLen);

    
    pktQueue[pos].head.pktFlag = 0;
    pktQueue[pos].head.rcvedLen=0;
    pktQueue[pos].head.totalLen = 0;
    pktQueue[pos].head.usedFlag = 1; 
    pktQueue[pos].head.timeStamp = (tv.tv_sec*1000 + tv.tv_usec/1000);
    pktQueue[pos].head.waiting = 0; 
    pktQueue[pos].head.waitCloseStatus = 0;
    bit_clear(pktQueue[pos].head.bit);
    pktQueue[pos].head.lenMap.clear();
    
    return pos;
}

int GetPktQueuePos(PackQueueInfo *pktQueue, unsigned int pktFlag, bool rallocflg)
{
    int i;
    int unusedPos = -1;
    struct timeval tv;
    struct timezone tz;
    (void)gettimeofday(&tv, &tz);
    RET_IF(pktQueue==NULL, unusedPos);
    for(i=0; i<RECV_PKT_MIN_QUEUE; i++)
    {
        if((rallocflg==true) && (unusedPos==-1) && (pktQueue[i].head.usedFlag == 1))
        {            
            pktQueue[i].head.timeStamp = (tv.tv_sec*1000 + tv.tv_usec/1000);
            unusedPos= i;
        }
        if(pktQueue[i].head.pktFlag == pktFlag)
        {
            unusedPos = i;
            break;
        } 
    }
    RET_IF(rallocflg==false, unusedPos);
    
    /* 未找到已经分配的可用buf或者自己存储的buff，尝试重新分配一个新的内存buff */
    if(unusedPos == -1)
    {
        unusedPos = InitOnePackQueue(&pktQueue);       
    }

    /* s释放一个占有buff最长时间的队列来接收新的报文 */
    if(unusedPos == -1)
    {
        unusedPos = ReleaseOneOverTimeQueue(pktQueue);       
    }
    return unusedPos;
}
int allocate_clientsocket(SocketInfo* psocketIds, int net_type, int remote_ipaddr, int remote_port, unsigned int pktFlag, unsigned int sgg_multi_lines, int &pos) {
    int sd = -1;
    unsigned i = 0;
    
    pos = sgg_multi_lines;
#ifdef _DEBUG_OFF_
    RET_IF(net_type >= NET_TYPE_BUTT, -1);
#endif
    for (i=0; i<sgg_multi_lines; i++) {
        BREAK_IF(psocketIds[i].socket_type == NET_TYPE_BUTT);
    }
    if (i < sgg_multi_lines) {
        pthread_mutex_lock(&(psocketIds[i].lock));
        psocketIds[i].socket_type = net_type;    
        psocketIds[i].remote_ip = remote_ipaddr;
        psocketIds[i].remote_port = remote_port;
        psocketIds[i].pktFlag = pktFlag;
        psocketIds[i].socket_id =-1;
        psocketIds[i].idleTimes =0;
        psocketIds[i].idleCrcles =0;
        //g_socketIds[i].lock = PTHREAD_MUTEX_INITIALIZER;
        pos = i;
    } else {
        return -1;
    }
    
    sd = psocketIds[i].socket_id;
    if(sd>=0){
        goto end;
        
    }
    
    if (net_type == NET_TYPE_TCP) {
        sd = run_tcpclient(remote_ipaddr, remote_port, 0,true);
    } else {
        sd = run_udpclient(remote_ipaddr, remote_port, 0);
    }

    if (sd >= 0) {
        psocketIds[i].socket_id = sd;
    }
    
    end:
    
    pthread_mutex_unlock(&(psocketIds[i].lock));
    DEBUG_PRINT("allocate_clientsocket:pktFlag=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u, socket_id=%d, pos=%d\r\n", 
            pktFlag, net_type, remote_ipaddr, remote_port, sd, pos);
    ELOG(L_INFO,"allocate_clientsocket:pktFlag=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u, socket_id=%d, pos=%d", 
        pktFlag, net_type, remote_ipaddr, remote_port, sd, pos);
    
    return 0;
}
void release_clientsocket(SocketInfo* psocketIds, unsigned int sgg_multi_lines, int pos) {
    int sd = -1;
    RET_VOID_IF(pos < 0 || pos >= (int)sgg_multi_lines);
    
    pthread_mutex_lock(&(psocketIds[pos].lock));
    sd = psocketIds[pos].socket_id;
    if (psocketIds[pos].socket_type == NET_TYPE_TCP) {
        close_tcpclient(sd);
    } else {
        close_udpclient(sd);
    }    
    psocketIds[pos].socket_id = -1;
    psocketIds[pos].socket_type = NET_TYPE_BUTT;
    pthread_mutex_unlock(&(psocketIds[pos].lock));
    
    DEBUG_PRINT("release_clientsocket:pos=%d, socket_id=%d\r\n", pos,sd);
    ELOG(L_INFO,"release_clientsocket:pos=%d, socket_id=%d", pos, sd);
    return;
}

int get_clientsocketinfo(SocketInfo* psocketIds, unsigned int sgg_multi_lines, int pos, int &socket_id, int isNeedClear) {    

    RET_IF(pos >= (int)sgg_multi_lines, -1);
    if(psocketIds[pos].socket_id >= 0) {
        pthread_mutex_lock(&(psocketIds[pos].lock));
        socket_id = psocketIds[pos].socket_id;
        if(isNeedClear==1) {
            psocketIds[pos].idleTimes = 0;
            psocketIds[pos].idleCrcles = 0;
        }
        pthread_mutex_unlock(&(psocketIds[pos].lock));
        return 0;
    } else if (psocketIds[pos].socket_type == NET_TYPE_BUTT) {
        return -1;
    }
    return 0;
}

int findOnePktByOffset(char*data, unsigned int offset)
{
    SendPackStr *ptr = (SendPackStr*)data;
    if(ptr !=NULL && ((ptr->head.offset%UDP_PACKET_ORDER_LEN_MAX) == (offset%UDP_PACKET_ORDER_LEN_MAX))) {
        return 0;
    } else {
        return -1;
    }   
}

int findOnePktByRelativeOffset(char*data, unsigned int offset)
{
    SendPackStr *ptr = (SendPackStr*)data;
    if(ptr !=NULL && ((ptr->head.offset%UDP_PACKET_ORDER_LEN_MAX) == (offset%UDP_PACKET_ORDER_LEN_MAX))) {
        return 0;
    } else {
        return -1;
    }   
}

void udpDataPrint(char*data, int len, void*para)
{
    SendPackStr *ptr = (SendPackStr*)data;
    if(ptr !=NULL){
       DEBUG_PRINT("resend pkt id:%d,%d,%d,0x%08X\n", ptr->head.pktFlag, ptr->head.offset,ptr->head.buf_size, ptr->head.magicWord); 
    }
    return;
}

void* udpRecvHeartTask(void *arg1/*, void *arg2, void*arg3*/)
{
    int sd = *((int*)arg1);
    int rv = 0;
    unsigned int *pdata;
    free(arg1);
    char buff[PER_PACKET_MAX_SIZE];
    VerifyMsgHead *pmsg;
    pmsg = (VerifyMsgHead*)buff;
    int num = 0;
    int retValue = 0;
    #ifndef _DEBUG_OFF_
    PSTACK ptr = NULL;    
    int flag = 0;
    #endif
    
    do{
        rv = recv(sd, buff, PER_PACKET_MAX_SIZE, 0);
        if((rv>0) && (rv > (int)sizeof(VerifyMsgHead)) && pmsg->magix == MAGIC_WORD_MAIN) {
            switch (pmsg->msgType)
            {
                case UDP_ACK:
                    pdata=(unsigned int*)(buff+sizeof(VerifyMsgHead));
                    UdpFlowCtlIns.ack(sd, *pdata);
                    ELOG(L_DEBUG, "=======udpRecvHeartTask_ACK=========sd=%d, len=%d, ack_num=%d\r\n", sd, rv, *pdata);
                    break;
                case UDP_RT_SPECIAL:
                    pdata=(unsigned int*)(buff+sizeof(VerifyMsgHead));
                    num = ((pmsg->msgLen-sizeof(VerifyMsgHead))/sizeof(int));
                    ELOG(L_DEBUG, "=======udpRecvHeartTask_SPECIAL=========sd=%d, len=%d, num=%d\r\n", sd, rv, *pdata);
                    while(num-->0){
                        retValue = UdpFlowCtlIns.resend(sd, *pdata);
                        #ifndef _DEBUG_OFF_
                        if (retValue >= 0){
                            ELOG(L_DEBUG, "resend pkt id:%d\n", *pdata);
                            flag++;
                        } 
                        #endif
                        pdata++;
                    }
                    #ifndef _DEBUG_OFF_
                    if(flag == 0 && UdpFlowCtlIns.ppFlow[sd] != NULL) {
                        ptr = UdpFlowCtlIns.ppFlow[sd]->pktBufStack;
                        TraversalStack(ptr, udpDataPrint, (void*)(&sd));
                    }
                    #endif
                    (void)retValue;                    
                    break;
                case UDP_RT_ALL:
                    pdata=(unsigned int*)(buff+sizeof(VerifyMsgHead));
                    ELOG(L_DEBUG, "=======udpRecvHeartTask_ALL=========sd=%d, len=%d,\r\n", sd, rv);
                    ELOG(L_INFO,"sd=%d, pktFlag=%u", sd, *pdata);
                    UdpFlowCtlIns.resendAll(sd, *pdata);
                    break;
            }
        }
        pthread_testcancel();
    } while(1);

    DEBUG_PRINT("=======udpRecvHeartTask_END=========%d\r\n", rv);
    
    
    return NULL;
}

std::set<int> g_udpZombileData;
void reSendUdpZombilePkt(char*data, int len, void*para)
{
    int sid = *((int*)para);
    SendPackStr*pkt = (SendPackStr*)data;
    send_udpdata(sid, data, pkt->head.buf_size+sizeof(UserHeadInfo), 10, false);
}
void* udpZombileDataProcTask(void *arg1)
{
    std::set<int>::iterator it;
    int sd;
    UserHeadInfo pktHead;
    do{
        pthread_mutex_lock(&(UdpFlowCtlIns.lock));
        pthread_cond_wait(&(UdpFlowCtlIns.cond), &(UdpFlowCtlIns.lock));  
        
        for (it=g_udpZombileData.begin(); it!=g_udpZombileData.end(); ++it) {
            sd = *it;
            TraversalStack(UdpFlowCtlIns.ppFlow[sd]->pktBufStack, reSendUdpZombilePkt, &sd);
            pktHead.pktTotalLen = 0;
            pktHead.buf_size = 0;
            pktHead.magicWord = MAGIC_WORD_BANK;
            pktHead.pktFlag = UdpFlowCtlIns.ppFlow[sd]->pktFlag;
            send_udpdata(sd, (char*)(&pktHead), sizeof(UserHeadInfo), 100, false);
            //UdpFlowCtlIns.resendAll(sd, *pdata);
            
            ELOG(L_INFO,"sd=%d", sd);
            DEBUG_PRINT("[%s][%d]:[%s] sd=%d\n", __FILE__, __LINE__, __FUNCTION__,sd);
        
            pthread_cancel(UdpFlowCtlIns.ppFlow[sd]->thrdId);
            UdpFlowCtlIns.ppFlow[sd]->thrdId = 0;
            DestroyStack(&(UdpFlowCtlIns.ppFlow[sd]->pktBufStack));
            free(UdpFlowCtlIns.ppFlow[sd]);
            UdpFlowCtlIns.ppFlow[sd] = NULL;
            close(sd);    
        }
        g_udpZombileData.clear();
        pthread_mutex_unlock(&(UdpFlowCtlIns.lock)); 
                
        pthread_testcancel();
    }while(1);
    return NULL;
}



UdpFlowControl::UdpFlowControl()
{
    //thrdPools = threadpool_create(UDP_SEND_TASK_MIN_NUM, UDP_SEND_TASK_MAX_NUM, UDP_RECV_TASK_MAX_NUM);
    RET_VOID_IF(g_udpHbFlag==0);
    ppFlow = (CacheMsgSlideWin**)malloc(sizeof(CacheMsgSlideWin**)*UDP_FLOW_NUM_MAX);    
    if (ppFlow == NULL) {
        ELOG(L_ERROR,"malloc memory failed");
        return;
    } else {
        ELOG(L_INFO,"malloc memory successful");
        memset(ppFlow, 0, (sizeof(CacheMsgSlideWin**)*UDP_FLOW_NUM_MAX));
    }

    for(int i=0; i<UDP_FLOW_NUM_MAX; i++) {
        ppFlow[i] = (CacheMsgSlideWin*)malloc(sizeof(CacheMsgSlideWin));
        if(ppFlow[i]==NULL){
            goto end;            
        }
        //pthread_rwlock_init(&(ppFlow[i]->rwlock),NULL);
        memset(ppFlow[i], 0, sizeof(CacheMsgSlideWin));
        pthread_mutex_init(&(ppFlow[i]->mutex), NULL);        
    }
    
    if (pthread_create(&thrdId, NULL, udpZombileDataProcTask, NULL) != 0)//创建接收信息线程
	{
		ELOG(L_ERROR,"create thread pools failed");
	} else {
        pthread_detach(thrdId);
	}
    g_udpZombileData.clear();
    return;
end:

    for(int j=0; j<UDP_FLOW_NUM_MAX; j++) {
        ppFlow[j] = (CacheMsgSlideWin*)malloc(sizeof(CacheMsgSlideWin));
        if(ppFlow[j]!=NULL){
            free(ppFlow[j]); 
            ppFlow[j] = NULL;
        }
    }

    if (ppFlow != NULL) {
        free(ppFlow);
        ppFlow = NULL;
    }
    return;

}

UdpFlowControl::~UdpFlowControl()
{
    //threadpool_destroy(thrdPools);
    RET_VOID_IF(g_udpHbFlag==0);
    int i = 0;
    pthread_cancel(thrdId);
    while(i<UDP_FLOW_NUM_MAX) {
        if(ppFlow[i] != NULL) {
            pthread_mutex_destroy(&(ppFlow[i]->mutex));
            free(ppFlow[i]);
            ppFlow[i]=NULL;
        }
    }
    free(ppFlow);
}


int UdpFlowControl::add(int sd)
{
    
    int retValue = 0;
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), -1);
    //ppFlow[sd] = (CacheMsgSlideWin*)malloc(sizeof(CacheMsgSlideWin));
    //RET_IF(ppFlow[sd]==NULL, -1);
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ppFlow[sd]->sockId =sd;
    ppFlow[sd]->idleTimes =0;
    ppFlow[sd]->existHeart =0;
    ppFlow[sd]->pktAcked = 0xFFFFFFFF;
    ppFlow[sd]->pktOffset = 0xFFFFFFFF;
    CreateStack(&(ppFlow[sd]->pktBufStack), (UDP_PACKET_ORDER_LEN_MAX+1), sizeof(SendPackStr));
    int *tmp = (int*)malloc(sizeof(int));
    *tmp = sd;
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
    
    //threadpool_add_task(thrdPools, udpRecvHeartTask, (void*)tmp, NULL, NULL);
    retValue= pthread_create(&(ppFlow[sd]->thrdId), NULL, udpRecvHeartTask, tmp);
    if (retValue != 0) {        
        DEBUG_PRINT("create ack thread error:%s \n", strerror(errno));
        return -1;
    }
    pthread_detach(ppFlow[sd]->thrdId);

    return 0;
}
int UdpFlowControl::rmv(int sd)
{
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), -1);

    /*if(ppFlow[sd]->pktOffset != ppFlow[sd]->pktAcked) {
        pthread_mutex_lock(&lock);
        pthread_cond_signal(&cond);        
        g_udpZombileData.insert(sd);
        pthread_mutex_unlock(&lock);
        DEBUG_PRINT("join in udp zombile data:%d \n", sd);
        return -2;
    } else {
        DEBUG_PRINT("rmv flow data successfule:sd=%d,pktOffset=%u, pktAcked=%u \n", sd, ppFlow[sd]->pktOffset, ppFlow[sd]->pktAcked);
    }*/
    ELOG(L_DEBUG, "rmv flow post proc:%d, %llu\r\n", sd, ppFlow[sd]->thrdId);
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    pthread_cancel(ppFlow[sd]->thrdId);
    ppFlow[sd]->thrdId = 0;
    DestroyStack(&(ppFlow[sd]->pktBufStack));
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
    
    return 0;
}

int UdpFlowControl::push(int sd, char*data, unsigned int dataLen)
{
    PSTACK ptr = NULL;
    char *pbuf = NULL;
    int ret = 0;
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), -1);
    SendPackStr *pkt = (SendPackStr*)data;
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ptr = ppFlow[sd]->pktBufStack;
    ret = PushStack(ptr, &pbuf);
    if (ret >= 0) {
        memcpy(pbuf, data, dataLen);
        ppFlow[sd]->pktOffset = pkt->head.offset;
        ppFlow[sd]->pktFlag = pkt->head.pktFlag;
        ppFlow[sd]->idleTimes = 0;
        //DEBUG_PRINT("#######push success\r\n")
        pthread_mutex_unlock(&(ppFlow[sd]->mutex));
        return 0;

    } else {
        #if 0                
        /* 连续超过20s闲置，说明对端发生异常，触发一次队列清空 */
        if (ppFlow[sd]->idleTimes > (UDP_TUNNEL_IDLE_TIMES*2000)) {
            ppFlow[sd]->idleTimes = 0;
            ppFlow[sd]->pktAcked = 0xFFFFFFFF;
            ClearStack(ptr);
            LOG_OF_DAY("[%s][%d]:[%s] udp send queue is full_2: sid=%d, pktoffset=%u\r\n", __FILE__,__LINE__,__FUNCTION__,sd, pkt->head.offset);
        }else if(ppFlow[sd]->idleTimes == (UDP_TUNNEL_IDLE_TIMES*1000)) {
            /* 连续闲置超过10个周期（1s），触发一次尝试发送本端队列中所有报文 */
            TraversalStack(ptr, reSendUdpZombilePkt, &sd);
            LOG_OF_DAY("[%s][%d]:[%s] udp send queue is full_1: sid=%d, pktoffset=%u\r\n", __FILE__,__LINE__,__FUNCTION__,sd, pkt->head.offset);
        }
        #endif
        pthread_mutex_unlock(&(ppFlow[sd]->mutex));        
        usleep(1000);
        ppFlow[sd]->idleTimes++;
        if (ppFlow[sd]->idleTimes > 11000) {
            return -ERR_UDP_SOCKET_ACK_STACK_FULL_WITHOUT_HEART;
        } else {
            return -ERR_UDP_SOCKET_ACK_STACK_FULL_WITH_HEART;
        }
    }
    //DEBUG_PRINT("#######find sd\r\n")    
}

int UdpFlowControl::pop(int sd, int len)
{
    PSTACK ptr = NULL;
    char *pbuf = NULL;
    int num = len;
    RET_IF(g_udpHbFlag==0, 0);
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), -1);
    
    ptr = ppFlow[sd]->pktBufStack;
    while(num-- > 0){
        PopStack(ptr, &pbuf);
    }
    
    return 0;
}

int UdpFlowControl::resend(int sd, unsigned int offset)
{
    PSTACK ptr = NULL;
    char *pbuf = NULL;
    int retvalue = 0;
    SendPackStr *pkt=NULL;
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), -2);  
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ptr = ppFlow[sd]->pktBufStack;
    retvalue = GetStackData(ptr, &pbuf, findOnePktByRelativeOffset, offset);
    if (retvalue >= 0){
        pkt = (SendPackStr*)pbuf;
        retvalue = send_udpdata(sd, pbuf, pkt->head.buf_size+sizeof(UserHeadInfo), 0, false);        
    }
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));

    return retvalue;
    
}

int UdpFlowControl::resendAll(int sd, unsigned int pktFlag)
{
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX)), -1);

    PSTACK ptr = ppFlow[sd]->pktBufStack;
    int retvalue = 0;
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    if (ppFlow[sd]->pktAcked == ppFlow[sd]->pktOffset) {
        UserHeadInfo head;
        head.buf_size = 0;
        head.magicWord = MAGIC_WORD_BANK;
        head.offset = 0;
        head.pktFlag = pktFlag;
        retvalue = send_udpdata(sd, (char*)(&head), sizeof(UserHeadInfo), 0, false);
    } else {
        retvalue = TraversalStack(ptr, reSendUdpZombilePkt, (void*)(&sd));
    }
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
    return retvalue;
}


int UdpFlowControl::ack(int sd, unsigned int offset)
{
    PSTACK ptr = NULL;
    int retvalue = 0;
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), -1);
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ptr = ppFlow[sd]->pktBufStack;
    retvalue = PopStackEx(ptr, findOnePktByOffset, offset);
    ppFlow[sd]->pktAcked = offset;
    ppFlow[sd]->existHeart++;
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
    return retvalue;
   
}

bool UdpFlowControl::isAllAcked(int sd)
{
    PSTACK ptr = NULL;
    bool retvalue = true;
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), true);
    
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ptr = ppFlow[sd]->pktBufStack;
    retvalue = IsStackEmpty(ptr);
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
    return retvalue;
}

void UdpFlowControl::resetHeartCounter(int sd)
{
    RET_VOID_IF((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL));
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ppFlow[sd]->existHeart = 0;
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
}

int UdpFlowControl::getHeartCounter(int sd)
{
    unsigned int ret = 0;
    RET_IF(((sd<0)||(sd>UDP_FLOW_NUM_MAX) || (ppFlow[sd]==NULL)), 0);
    pthread_mutex_lock(&(ppFlow[sd]->mutex));
    ret = ppFlow[sd]->existHeart;
    pthread_mutex_unlock(&(ppFlow[sd]->mutex));
    return ret;
}

UdpFlowControl& UdpFlowControl::GetInstance()
{
    static UdpFlowControl instace;    
    return instace;
}

int GetUdpPktHeartCounter(int sd)
{
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
    return UdpFlowCtlIns.getHeartCounter(sd);
}

void ResetUdpPktHeartCounter(int sd)
{
    /* 不支持udp心跳包则直接返回 */
    RET_VOID_IF(g_udpHbFlag==0);
    
    UdpFlowCtlIns.resetHeartCounter(sd);
}



int run_udpclient(int ipaddr, int port, int sendInterval)
{
	int client_sd;
	int con_rv;
	struct sockaddr_in client_sockaddr; //定义IP地址结构
	socklen_t addrlen = sizeof(client_sockaddr);
	
	client_sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (client_sd == -1) {
		DEBUG_PRINT("create udp send client error:%s \n", strerror(errno));
		return -ERR_UDP_SOCKET_CLIENT_CREATE_FAILED;
	}
	memset(&client_sockaddr, 0, sizeof(client_sockaddr));
	client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
	client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
	client_sockaddr.sin_addr.s_addr = htonl(ipaddr);//将字符串的ip地址转换成int型,客服端要连接的ip地址
	con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr,sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
	if (con_rv == -1) {
		DEBUG_PRINT("udp connect error:%s \n", strerror(errno));
		return -ERR_UDP_SOCKET_CLIENT_CONNECT_FAILED;
	}

    /*struct timeval timeout = {1,0};  
    if(setsockopt(client_sd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
    {
        DEBUG_PRINT("[run_tcpclient] setsockopt(SO_RCVTIMEO) error:%s \n", strerror(errno));
        return -ERR_TCP_SOCKET_SET_OPT_FAILED;
    }*/
    getsockname(client_sd, (struct sockaddr*) &client_sockaddr, &addrlen);
    
    UdpFlowCtlIns.add(client_sd);
    DEBUG_PRINT("create udp send client succesful: sid=%d\r\n", client_sd);
    ELOG(L_INFO,"create udp send client succesful: sid=%d, src_port=%d", client_sd, ntohs(client_sockaddr.sin_port));
	return client_sd;
}

void close_udpclient(int sid)
{    
    int ret = 0;
    if (sid >= 0) {
        ret = UdpFlowCtlIns.rmv(sid);
        if(ret != -2){
            close(sid); 
        }        
    }    
    
    DEBUG_PRINT("close udp send client succesful:sid=%d, ret=%d\r\n", sid, ret);	
    ELOG(L_INFO,"close udp send client succesful: sid=%d, ret=%d", sid, ret);
	return;
}

int send_udpdata(int sid, char*buf, int dataLen, unsigned int send_interval, bool cacheFlag)
{
    int sendDataNum = -1;  
    int ret = 0;
    int times = 0;
    
 	if (sid < 0 || buf == NULL || dataLen <= 0) {
		DEBUG_PRINT("udp send_data error:para is invalid\r\n");
		return -ERR_UDP_SOCKET_PARA_INVALID;
	}

    if((g_udpHbFlag==1) && cacheFlag) {
        do {
           ret = UdpFlowCtlIns.push(sid, buf, dataLen);
           times++;
        }while(ret<0 && times<5000);
        if(ret<0) {
            ELOG(L_DEBUG, "udp ack stack is full:sid=%d\r\n", sid);
            return ret;
        }
    }
    
    sendDataNum = send(sid, buf, dataLen, 0);
    if (sendDataNum != dataLen)
    {
        DEBUG_PRINT("udp send error:sid=%d, dataLen=%u, errmsg=%s \n", sid, dataLen, strerror(errno));
        return -ERR_UDP_SOCKET_CLIENT_SEND_FAILED;
    }

    

    DEBUG_PRINT("send_udpdata: udp send data success:%d,%d \n", sendDataNum,send_interval);
    
    if (send_interval > 0) {
        usleep(send_interval);
    }
        
	return 0;
}

int sendto_udpdata(int sid, char*buf, int dataLen, unsigned int send_interval, unsigned int dstIp, unsigned int dstPort)
{
    int sendDataNum = -1;  
    struct sockaddr_in dst_addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    VerifyMsgHead *pHead = (VerifyMsgHead*)buf;
    int* temp = (int*)(pHead+1);

    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);
    
 	if (sid < 0 || buf == NULL || dataLen <= 0) {
		DEBUG_PRINT("udp sendto_data error:para is invalid:sid=%d, dataLen=%d\r\n", sid, dataLen);
		return -ERR_UDP_SOCKET_PARA_INVALID;
	}

    memset(&dst_addr, 0, sizeof(struct sockaddr_in));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_addr.s_addr = htonl(dstIp);
    dst_addr.sin_port = htons(dstPort);
    sendDataNum = sendto(sid, buf, dataLen, 0, (struct sockaddr*)&dst_addr, addrlen);
    if (sendDataNum != dataLen)
    {
        DEBUG_PRINT("udp sendto error:sid=%d, dataLen=%u, dstIp=0x%08X, dstPort=%d, errmsg=%s \n", sid, dataLen, dstIp, dstPort, strerror(errno));
        return -ERR_UDP_SOCKET_CLIENT_SEND_FAILED;
    }   

    ELOG(L_DEBUG, "sendto_udpdata: udp send data success:sid=%d, sendDataNum=%d,dstIp=0x%08X, dstPort=%d,msgType=%d, ack_num=%d, send_interval=%d \n", sid, sendDataNum,dstIp, dstPort,pHead->msgType,*temp,send_interval);
    
    if (send_interval > 0) {
        usleep(send_interval);
    }
        
	return 0;
}

bool checkAllUdpPktsAcked(int sid)
{
    /* 不支持心跳机制的，直接返回全部已经确认 */
    RET_IF(g_udpHbFlag==0, true);    
    return UdpFlowCtlIns.isAllAcked(sid);
}

/*void *addUDPTask(void *arg1, void *arg2, void*arg3)
{
    udp_thread_para *pthreadpara = (udp_thread_para*)arg1;

    if (pthreadpara->socket_type == NET_TYPE_UDP) {
        send_udpdata(pthreadpara->send_sid, pthreadpara->pbuf, pthreadpara->datalen);
    } else if (pthreadpara->socket_type == NET_TYPE_TCP) {
        send_tcpdata(pthreadpara->send_sid, pthreadpara->pbuf, pthreadpara->datalen);
    }
    
    DEBUG_PRINT("[addUDPTask] send received udp data:%s\r\n", pthreadpara->pbuf);
    return NULL;
}*/

static int create_udpsocket(int ipaddr, int port, int rcvBufMaxSize)
{ 
	int socket_id;
	struct sockaddr_in sockaddr; //定义IP地址结构
	int on = 1;
	socket_id = socket(AF_INET, SOCK_DGRAM, 0); //初始化socket
	if (socket_id == -1)
	{
		DEBUG_PRINT("socket create error:%s \n", strerror(errno));
		return -1;
	}

    // 查看系统默认的socket接收缓冲区大小
    int defRcvBufSize = -1;
    socklen_t optlen = sizeof(defRcvBufSize);
    if (getsockopt(socket_id, SOL_SOCKET, SO_RCVBUF, &defRcvBufSize, &optlen) < 0)
    {
        DEBUG_PRINT("getsockopt error=%d(%s)!!!\n", errno, strerror(errno));
        return -1;
    }
    ELOG(L_INFO,"OS default udp socket recv buff size is: %d,%d", defRcvBufSize, optlen);

    optlen = sizeof(rcvBufMaxSize);
    if (/*(rcvBufMaxSize>defRcvBufSize) && */(setsockopt(socket_id, SOL_SOCKET, SO_RCVBUF, &rcvBufMaxSize, optlen) < 0))
    {
        DEBUG_PRINT("setsockopt error=%d(%s)!!!\n", errno, strerror(errno));
        return -1;
    }

	if (setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) //设置ip地址可重用
	{
		DEBUG_PRINT("setsockopt error:%s \n", strerror(errno));
		return -1;
	}

    struct timeval timeout = {1,0};  
    if(setsockopt(socket_id, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
    {
        DEBUG_PRINT("setsockopt error:%s \n", strerror(errno));
		return -ERR_UDP_SOCKET_SET_OPT_FAILED;
    }
    
	sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
	sockaddr.sin_family = AF_INET;    //设置结构类型为TCP/IP
	if (ipaddr != 0) {
        sockaddr.sin_addr.s_addr = htonl(ipaddr);
	}
    else {
        sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);//这里写一个长量INADDR_ANY表示server上所有ip，这个一个server可能有多个ip地址，因为可能有多块网卡
    }
    
	if (bind(socket_id, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) == -1)
	{
		DEBUG_PRINT("bind error:%s \n", strerror(errno));
		return -1;
	}

	return socket_id;
}


static void* thread_udppktrecv(void *arg1/*, void * arg2, void * arg3*/)
{
    udp_thread_recv_para *pthdrecvpara = (udp_thread_recv_para*)arg1;
	int sd = pthdrecvpara->listen_sid;
    int location = pthdrecvpara->location;
    UDPRecvDataQueue *pUDPRecvDataQueues = pthdrecvpara->pUDPRecvDataQueues;
    char *pbuf = NULL;
    int wpos;
    SendPackStr *sendpkt = NULL;
    int offset = 0;
    unsigned int fulltimes = 0;
    Pkt5Tuple pktBaseInfo;
    //struct sockaddr src_addr;
    struct sockaddr_in src_addr;
    socklen_t addrlen;
    #ifndef _DEBUG_OFF_
    //static int nmmm=0;
    //int pos = 0;
    #endif

    //free(arg1);    
    DEBUG_PRINT("thread_udppktrecv in(sd=%d, stopflag=%d).....\n", sd, g_stopflag);
    ELOG(L_INFO,"udp recv thread ining(listen_ip=0x%08x, port=%u, sid=%d).", pthdrecvpara->listen_ip, pthdrecvpara->listen_port,sd);
    //LOG_OF_DAY("[%s][%d]: thread_recv in(sd=%d).....\n", __FILE__, __LINE__, sd);

    
    offset = (location == NET_LOCATION_CLIENT)?sizeof(UserHeadInfo):0;
    pktBaseInfo.dstIP = pthdrecvpara->listen_ip;
    pktBaseInfo.dstPort = pthdrecvpara->listen_port;
    pktBaseInfo.protocol = NET_TYPE_UDP;

	while (!pthdrecvpara->shutdown)
	{
        wpos = pUDPRecvDataQueues->wpos; 
        if(PushStack(pUDPRecvDataQueues->PktBufStack[wpos], &pbuf)==-1)
        {
            if (wpos == pUDPRecvDataQueues->rpos) {
                pthread_mutex_lock(&(pUDPRecvDataQueues->lock));
                pthread_cond_signal(&(pUDPRecvDataQueues->cond));
                pUDPRecvDataQueues->wpos = ((++wpos)%UDP_RECV_QUEUE_SIZE);
                pthread_mutex_unlock(&(pUDPRecvDataQueues->lock));
            } else {
                ++fulltimes;
                if((fulltimes == 1)|| ((fulltimes%10000) == 0)) {
                    ELOG(L_WARNING,"thread_udppktrecv: udp recv queue is full[%u, %u].", fulltimes, wpos);
                }
                usleep(100);
            }
            continue;
        }

        
		//int rv = recv(sd, pbuf+offset, PER_PACKET_MAX_SIZE-offset, 0); //半阻塞的，当前阻塞1s
		addrlen = sizeof(struct sockaddr_in);
		int rv = recvfrom(sd, pbuf+offset, PER_PACKET_MAX_SIZE-offset, 0, (struct sockaddr*)&src_addr, &addrlen);
        if (rv <= 0) {
            BackPushStack(pUDPRecvDataQueues->PktBufStack[wpos]);
        } else {
            sendpkt = (SendPackStr*)pbuf;
            sendpkt->head.recvFromIp = ntohl(src_addr.sin_addr.s_addr);
            sendpkt->head.recvFromPort = ntohs(src_addr.sin_port);
            if (location == NET_LOCATION_CLIENT) {                
                sendpkt->head.buf_size = rv;
                /* UDP报文审计接口 */
                pktBaseInfo.srcIP = sendpkt->head.recvFromIp;
                pktBaseInfo.srcPort = sendpkt->head.recvFromPort;            
                (void)sgg_RtpAuditAdd(pktBaseInfo, pbuf+offset, rv, pthdrecvpara->rtpCfgFlag);
            }
            DEBUG_PRINT("thread_udppktrecv(ip=0x%08x, port=%u,):%d, %d, %d, %s\r\n",sendpkt->head.recvFromIp, sendpkt->head.recvFromPort,rv, sendpkt->head.pktFlag, sendpkt->head.offset, pbuf+offset);             
            
            /*LOG_OF_DAY("thread_udppktrecv:%d, %d, %d, %d\r\n",rv, sendpkt->head.pktFlag, sendpkt->head.offset, sendpkt->head.buf_size); */
            /*#ifndef _DEBUG_OFF_
            if((sendpkt->head.offset == 10)&& nmmm<1) {
                nmmm++;
                BackPushStack(g_UDPRecvDataQueues.PktBufStack[wpos]);
                DEBUG_PRINT("drop pkt:offset=%d,nmmm=%d\n", sendpkt->head.offset, nmmm);
                pos = GetPktQueuePos(sendpkt->head.pktFlag, false);
            }
            if(pos>=0) {
                DEBUG_PRINT("waiting flag:%d\n", g_pktQueue[pos].head.waiting);
            }
            #endif*/
        }
      
        if (wpos == pUDPRecvDataQueues->rpos && !IsStackEmpty(pUDPRecvDataQueues->PktBufStack[wpos])) {
            pthread_mutex_lock(&(pUDPRecvDataQueues->lock));
            pthread_cond_signal(&(pUDPRecvDataQueues->cond));
            pUDPRecvDataQueues->wpos = ((++wpos)%UDP_RECV_QUEUE_SIZE);
            pthread_mutex_unlock(&(pUDPRecvDataQueues->lock));
        }       
        
        pthread_testcancel();
    }

    /* 外部强制退出，通知读线程退出 */
    if(pthdrecvpara->shutdown==1) {
        pthread_mutex_lock(&(pUDPRecvDataQueues->lock));
        pthread_cond_broadcast(&(pUDPRecvDataQueues->cond));
        pthread_mutex_unlock(&(pUDPRecvDataQueues->lock));
    }
    //close(sd);
    //pthread_cancel(g_serverpktsendthrd);
    DEBUG_PRINT("thread_pktrecv out.....\n");
    ELOG(L_INFO,"udp recv thread outing(listen_ip=0x%08x, port=%u, sid=%d).", pthdrecvpara->listen_ip, pthdrecvpara->listen_port,sd);
    pthread_exit((void *) 0);
    return NULL;
}


static void* thread_pktsend(void *arg1/*, void * arg2, void * arg3*/)
{
    udp_thread_send_para *recvHandle = (udp_thread_send_para *) arg1;
    int net_type = recvHandle->remote_socket_type;
    unsigned int remote_ip = recvHandle->remote_ip;
    unsigned int remote_port = recvHandle->remote_port;
    unsigned int udp_send_interval = recvHandle->udp_send_interval;
    unsigned int sgg_multi_lines = recvHandle->sgg_multi_lines;
    int listen_sid =  recvHandle->listen_sid;
    SocketInfo* socketIds = recvHandle->socketIds;
    PackQueueInfo *pktQueue = recvHandle->pktQueue;
    UDPRecvDataQueue *pUDPRecvDataQueues = recvHandle->pUDPRecvDataQueues;
    int spos = -1;
    int send_sid = -1;
    int location = recvHandle->location;
    int retValue = 0;
    int pos = -1;
    char bitvalue;
    int rpos;
    int needUpdateWaitFlag = 0;
    char *pbuf;
    SendPackStr *pPktInfo; 
    unsigned int offset = 0;
    unsigned int offset2 = 0;
    unsigned int ackNum = 0;
    std::map<unsigned int,int> *ppid2sidmap = &(recvHandle->pktFlagMaps->pid2sidmap);
    std::map<unsigned int,int>::iterator itPid2Sid;
    unsigned long droppkts = 0;
    std::map<Pkt3Tuple,pktFlagWithOffset> *ppktFlagmap = &(recvHandle->pktFlagMaps->pktFlagmap);
    std::map<Pkt3Tuple,pktFlagWithOffset>::iterator itPktFlag;
    pthread_mutex_t mutex = recvHandle->pktFlagMaps->mutex;
    unsigned int pktid = 0;
    Pkt3Tuple pktFlagKey;
    pktFlagWithOffset pktFlagData;

    char ackBuff[512];//sizeof(VerifyMsgHead)+sizeof(int)*n
    VerifyMsgHead *packHead = (VerifyMsgHead*)ackBuff;
    unsigned int *ptr = (unsigned int*)(packHead+1);
    packHead->magix=MAGIC_WORD_MAIN;
    packHead->version = 100;
    packHead->errCode = 0;
    
    

    /* 提前释放内存，防止线程退出无法及时释放内存，造成内存泄漏 */
    //free(recvHandle);
    //recvHandle = NULL;

    
    DEBUG_PRINT("thread_pktsend in(location=%d, net_type=%d, sid_pos=%d, stop_flag=%d, udpHbFlag=%d).....\n", location, net_type, spos, g_stopflag, g_udpHbFlag);
    ELOG(L_INFO,"udp send thead ining(remote_socket_type=%d, remote_ip=0x%08X,remote_port=%u,socket_id=%d).....", 
        net_type, remote_ip,remote_port, send_sid);
    
    while (!recvHandle->shutdown) {
        rpos = pUDPRecvDataQueues->rpos;
        if (IsStackEmpty(pUDPRecvDataQueues->PktBufStack[rpos])) {
            pthread_mutex_lock(&(pUDPRecvDataQueues->lock));
            pUDPRecvDataQueues->rpos = pUDPRecvDataQueues->wpos;            
            pthread_cond_wait(&(pUDPRecvDataQueues->cond), &(pUDPRecvDataQueues->lock));  
            rpos = pUDPRecvDataQueues->rpos;
            pthread_mutex_unlock(&(pUDPRecvDataQueues->lock));            
        } 
        
        retValue = PopStack(pUDPRecvDataQueues->PktBufStack[rpos], &pbuf); 
        if (retValue!=-1)
        {
            pPktInfo = (SendPackStr*)pbuf;

            if (location == NET_LOCATION_CLIENT) {                
                pktFlagKey.ip = pPktInfo->head.recvFromIp;
                pktFlagKey.port = pPktInfo->head.recvFromPort;
                pktFlagKey.protocol = NET_TYPE_UDP;
                pthread_mutex_lock(&mutex);
                itPktFlag = ppktFlagmap->find(pktFlagKey);
                if(itPktFlag==ppktFlagmap->end()) {
                    pktid += 2;
                    pktFlagData.pktFlag = pktid;
                    pktFlagData.pktOffset = 0;
                    ppktFlagmap->insert(std::pair<Pkt3Tuple,pktFlagWithOffset>(pktFlagKey, pktFlagData));
                    pPktInfo->head.pktFlag = pktid;
                    pPktInfo->head.offset = 0;
                } else {
                    pPktInfo->head.pktFlag = (itPktFlag->second).pktFlag;
                    pPktInfo->head.offset = (++((itPktFlag->second).pktOffset));
                }
                pthread_mutex_unlock(&mutex);
            } else {
                if (pPktInfo->head.magicWord != MAGIC_WORD_MAIN) {
                    ELOG(L_DEBUG, "invalid packet:pktFlag=%u, buf_size=%d", pPktInfo->head.pktFlag, pPktInfo->head.buf_size);
                    continue;
                }
            }

            pthread_mutex_lock(&mutex);
            itPid2Sid = ppid2sidmap->find(pPktInfo->head.pktFlag);
            if(itPid2Sid==ppid2sidmap->end()) {
                retValue = allocate_clientsocket(socketIds, net_type, remote_ip, remote_port, pPktInfo->head.pktFlag, sgg_multi_lines, spos);
                if (retValue < 0) {
                    ELOG(L_ERROR,"create client socket failed:pktFlag=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u",  
                        pPktInfo->head.pktFlag, net_type, remote_ip, remote_port);
                    DEBUG_PRINT("create client socket failed:socket_type=%d, remote_ip=0x%08X,remote_port=%u\r\n", 
                        net_type, remote_ip, remote_port);
                } else {
                    ELOG(L_INFO,"create client socket success:pktFlag=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u, spos=%d",  
                        pPktInfo->head.pktFlag, net_type, remote_ip, remote_port, spos);
                }
                ppid2sidmap->insert(std::pair<int,int>(pPktInfo->head.pktFlag,spos));
                (void)get_clientsocketinfo(socketIds, sgg_multi_lines, spos, send_sid, 1);
                //EXEC_IF(send_sid<0, BackPopStack(g_UDPRecvDataQueues.PktBufStack[rpos]));
                //CONTINUE_IF(send_sid<0);
            } else {
                spos = itPid2Sid->second;
                retValue = get_clientsocketinfo(socketIds, sgg_multi_lines, spos, send_sid, 1);
                if(retValue<0){
                    retValue = allocate_clientsocket(socketIds, net_type, remote_ip, remote_port, pPktInfo->head.pktFlag, sgg_multi_lines, spos); 
                    if (retValue >= 0) {
                        (void)get_clientsocketinfo(socketIds, sgg_multi_lines, spos, send_sid, 1);
                        needUpdateWaitFlag = 1;
                    }
                }
                //EXEC_IF(send_sid<0, BackPopStack(g_UDPRecvDataQueues.PktBufStack[rpos]));
                //CONTINUE_IF(send_sid<0);
            } 
            pthread_mutex_unlock(&mutex);

            if (location == NET_LOCATION_CLIENT) {
                /* 发送端的UDP Server需要增加报文头部进行发送 */
                pPktInfo->head.magicWord = MAGIC_WORD_MAIN;
                pPktInfo->head.pktTotalLen = pPktInfo->head.buf_size;
                //pPktInfo->head.offset = offset++;                
                if (net_type == NET_TYPE_UDP) {
                    retValue = send_udpdata(send_sid, pbuf, (sizeof(UserHeadInfo)+pPktInfo->head.buf_size), udp_send_interval, true);
                    //printf("thread_pktsend:%s\r\n", pbuf+sizeof(PackInfo));
                    DEBUG_PRINT("[%s][%d]:[%s], pktFlag=%u, offset=%u, recvFromIp=0x%08X, recvFromPort=%u, retValue=%d\r\n", __FILE__, __LINE__, __FUNCTION__, 
                        pPktInfo->head.pktFlag, pPktInfo->head.offset, pPktInfo->head.recvFromIp, pPktInfo->head.recvFromPort, retValue);
                    EXEC_IF((retValue == -ERR_UDP_SOCKET_ACK_STACK_FULL_WITH_HEART), BackPopStack(pUDPRecvDataQueues->PktBufStack[rpos]));
                } else if (net_type == NET_TYPE_TCP) {
                    retValue = send_tcpdata(send_sid, pbuf, (sizeof(UserHeadInfo)+pPktInfo->head.buf_size));
                }
                //continue;
            } else {  
                /* 接收端的UDP Server需要组包进行完整性校验发送 */                              
                        
                pos = GetPktQueuePos(pktQueue,pPktInfo->head.pktFlag, true);
                if(pos == -1) {
                    if(pPktInfo->head.magicWord==MAGIC_WORD_MAIN) {
                        DEBUG_PRINT("packet reassembly queue is full\r\n");
                        //LOG_OF_DAY("[%s][%d]: packet reassembly queue is full",__FILE__, __LINE__);                    
                    }
                    continue;
                }
                pktQueue[pos].head.usedFlag = 2;
                pktQueue[pos].head.pktFlag = pPktInfo->head.pktFlag;
                if(needUpdateWaitFlag==1) {
                    needUpdateWaitFlag = 0;                    
                    /*LOG_OF_DAY("[%s][%d]:[%s] update waiting flag:pktFlag=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u, old=%u,new=%u\r\n", __FILE__, __LINE__, __FUNCTION__,
                            pPktInfo->head.pktFlag, net_type, remote_ip, remote_port, g_pktQueue[pos].head.waiting, (pPktInfo->head.offset% UDP_PACKET_ORDER_LEN_MAX));*/
                    pktQueue[pos].head.waiting = pPktInfo->head.offset% UDP_PACKET_ORDER_LEN_MAX;
                }
                
                if (pPktInfo->head.magicWord == MAGIC_WORD_BANK) {                
                    /* 进入缓存队列尾包处理流程，如果收到最后一个结束包，但是缓存队列中仍然有报文缺失，需要进入守护进程处理 */
                    if((g_udpHbFlag==1) && ((pPktInfo->head.offset%UDP_PACKET_ORDER_LEN_MAX) != pktQueue[pos].head.waiting)) {
                        pktQueue[pos].head.waitCloseStatus = 1;
                        pktQueue[pos].head.endOffset = pPktInfo->head.offset;
                        ELOG(L_INFO,"set close waiting flag:pktFlag=%u, offset=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u,waiting=%u", 
                                pPktInfo->head.pktFlag, pPktInfo->head.offset, net_type, remote_ip, remote_port, pktQueue[pos].head.waiting);
                        packHead->msgType = UDP_RT_SPECIAL;
                        packHead->msgLen = sizeof(VerifyMsgHead)+sizeof(int);
                        *ptr = pktQueue[pos].head.waiting;
                        sendto_udpdata(listen_sid, ackBuff, packHead->msgLen, 0, pPktInfo->head.recvFromIp, pPktInfo->head.recvFromPort);
                        
                    } else {
                        pthread_mutex_lock(&mutex);
                        ppid2sidmap->erase(pPktInfo->head.pktFlag);
                        pthread_mutex_unlock(&mutex);
                        release_clientsocket(socketIds, sgg_multi_lines, spos);
                        ELOG(L_INFO,"release_clientsocket success:pktFlag=%u, offset=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u,waiting=%u", 
                            pPktInfo->head.pktFlag, pPktInfo->head.offset, net_type, remote_ip, remote_port, pktQueue[pos].head.waiting);
                        pktQueue[pos].head.pktFlag = 0;
                        pktQueue[pos].head.totalLen = 0;
                        pktQueue[pos].head.rcvedLen=0;
                        pktQueue[pos].head.usedFlag = 1;                  
                        pktQueue[pos].head.waiting = 0; 
                        pktQueue[pos].head.waitCloseStatus = 0;
                        bit_clear(pktQueue[pos].head.bit);
                        pktQueue[pos].head.lenMap.clear();
                    }
                    continue;
                }                

                offset = pPktInfo->head.offset % UDP_PACKET_ORDER_LEN_MAX;
                ackNum = pPktInfo->head.offset;
                //直接发送
                /*LOG_OF_DAY("1111:%2d,%2d,%2d,%8d,%2d,%8d \r\n", pos, pktQueue[pos].head.waiting, offset, 0, pPktInfo->head.pktFlag,pPktInfo->head.buf_size);*/
                if (pktQueue[pos].head.waiting == offset) {
                    offset2 = offset;
                    if (net_type == NET_TYPE_UDP) {
                        retValue = send_udpdata(send_sid, pPktInfo->buf, pPktInfo->head.buf_size, udp_send_interval, false);
                        //printf("send_data udp:%d,%d,%d,%s\r\n", send_sid, udp_send_interval, pPktInfo->head.buf_size, pPktInfo->buf);
                    } else if (net_type == NET_TYPE_TCP) {
                        retValue = send_tcpdata(send_sid, pPktInfo->buf, pPktInfo->head.buf_size);
                    }
                    //EXEC_IF(retValue < 0, BackPopStack(g_UDPRecvDataQueues.PktBufStack[rpos]));
                    //CONTINUE_IF(retValue < 0);
                    pktQueue[pos].head.waiting = ((offset +1)%UDP_PACKET_ORDER_LEN_MAX);
                    bit_set(pktQueue[pos].head.bit,offset,0);
                    pktQueue[pos].head.lenMap.erase(offset); 

                    /* 支持UDP心跳报文 */
                    if (g_udpHbFlag==1) {
                        /* 发送缓存的报文 */
                        do {
                            offset2 = ((offset2+1)%UDP_PACKET_ORDER_LEN_MAX);
                            bitvalue = bit_get(pktQueue[pos].head.bit, offset2);
                            if (bitvalue==1) {
                                if (net_type == NET_TYPE_UDP) {
                                retValue = send_udpdata(send_sid, pktQueue[pos].buf+(offset2*PER_PACKET_MAX_SIZE), 
                                                        pktQueue[pos].head.lenMap[offset2], udp_send_interval, false);
                                } else if (net_type == NET_TYPE_TCP) {
                                    //printf("2222:%d,%d\r\n", offset2, g_pktQueue[pos].head.lenMap[offset2]);
                                    retValue = send_tcpdata(send_sid, pktQueue[pos].buf+(offset2*PER_PACKET_MAX_SIZE), pktQueue[pos].head.lenMap[offset2]);
                                }
                                pktQueue[pos].head.waiting = ((offset2 +1)%UDP_PACKET_ORDER_LEN_MAX);
                                bit_set(pktQueue[pos].head.bit,offset2,0);
                                pktQueue[pos].head.lenMap.erase(offset2);
                                ackNum++;
                            } else {
                                offset2 = ((offset2+UDP_PACKET_ORDER_LEN_MAX-1)%UDP_PACKET_ORDER_LEN_MAX);
                                break;
                            }                        
                        }while(offset2 != offset);

                        /* 发送确认序列号报文 */
                        packHead->msgType = UDP_ACK;
                        packHead->msgLen = sizeof(VerifyMsgHead)+sizeof(int);
                        *ptr = ackNum;
                        sendto_udpdata(listen_sid, ackBuff, packHead->msgLen, 0, pPktInfo->head.recvFromIp, pPktInfo->head.recvFromPort);
                    }
                        
                } else if (pktQueue[pos].head.waiting == ((offset+1)%UDP_PACKET_ORDER_LEN_MAX)){
                    offset2 = offset;
                    bit_set(pktQueue[pos].head.bit,offset,1);
                    memcpy(pktQueue[pos].buf+(offset*PER_PACKET_MAX_SIZE), pPktInfo->buf, pPktInfo->head.buf_size);
                    pktQueue[pos].head.recvFromIp = pPktInfo->head.recvFromIp;
                    pktQueue[pos].head.recvFromPort = pPktInfo->head.recvFromPort;
                    pktQueue[pos].head.lenMap[offset] = pPktInfo->head.buf_size;
                    
                    packHead->msgType = UDP_RT_SPECIAL;
                    packHead->msgLen = sizeof(VerifyMsgHead)+sizeof(int);
                    *ptr = (++ackNum-UDP_PACKET_ORDER_LEN_MAX);
                    sendto_udpdata(listen_sid, ackBuff, packHead->msgLen, 0, pPktInfo->head.recvFromIp, pPktInfo->head.recvFromPort);
                        
                    if(g_udpHbFlag == 0) {
                        bool issend = false;
                        do{
                            offset2 = ((offset2+1)%UDP_PACKET_ORDER_LEN_MAX);
                            bitvalue = bit_get(pktQueue[pos].head.bit, offset2);
                            if (bitvalue==0) {
                                droppkts++;
                                pktQueue[pos].head.waiting = offset2;
                                if ((droppkts==1) || ((droppkts % 100)==0)) {
                                    ELOG(L_INFO,"thread_pktsend: pktflag=%u, droppkts=%u", pktQueue[pos].head.pktFlag ,droppkts);
                                }
                                CONTINUE_IF(issend==false);
                                BREAK_IF(issend==true);
                            }

                            if (net_type == NET_TYPE_UDP) {
                                retValue = send_udpdata(send_sid, pktQueue[pos].buf+(offset2*PER_PACKET_MAX_SIZE), 
                                                        pktQueue[pos].head.lenMap[offset2], udp_send_interval, false);
                            } else if (net_type == NET_TYPE_TCP) {
                                //printf("2222:%d,%d\r\n", offset2, g_pktQueue[pos].head.lenMap[offset2]);
                                retValue = send_tcpdata(send_sid, pktQueue[pos].buf+(offset2*PER_PACKET_MAX_SIZE), pktQueue[pos].head.lenMap[offset2]);
                            }
                            //BREAK_IF(retValue<0);
                            issend = true;
                            pktQueue[pos].head.waiting = ((offset2 +1)%UDP_PACKET_ORDER_LEN_MAX);
                            bit_set(pktQueue[pos].head.bit,offset2,0);
                            pktQueue[pos].head.lenMap.erase(offset2);                   
                        }while(offset2 != offset);

                        /* 回退一个节点 */
                        if(retValue < 0) {
                            pktQueue[pos].head.waiting = ((offset2+UDP_PACKET_ORDER_LEN_MAX-1)%UDP_PACKET_ORDER_LEN_MAX);
                            BackPopStack(pUDPRecvDataQueues->PktBufStack[rpos]);
                        }
                    }
                    
                }else {
                    bit_set(pktQueue[pos].head.bit,offset,1);
                    memcpy(pktQueue[pos].buf+(offset*PER_PACKET_MAX_SIZE), pPktInfo->buf, pPktInfo->head.buf_size);
                    pktQueue[pos].head.pktFlag = pPktInfo->head.pktFlag;  
                    pktQueue[pos].head.recvFromIp = pPktInfo->head.recvFromIp;
                    pktQueue[pos].head.recvFromPort = pPktInfo->head.recvFromPort;
                    pktQueue[pos].head.lenMap[offset] = pPktInfo->head.buf_size;
                }
                if((g_udpHbFlag == 1) && (pktQueue[pos].head.waitCloseStatus == 1)) {
                    if((pktQueue[pos].head.waiting == (pktQueue[pos].head.endOffset%UDP_PACKET_ORDER_LEN_MAX))) {
                        pthread_mutex_lock(&mutex);
                        ppid2sidmap->erase(pPktInfo->head.pktFlag);
                        pthread_mutex_unlock(&mutex);
                        release_clientsocket(socketIds, sgg_multi_lines, spos);
                        ELOG(L_INFO,"ELOG(L_INFO,release_clientsocket success:pktFlag=%u, offset=%u, socket_type=%d, remote_ip=0x%08X,remote_port=%u,waiting=%u", __FILE__, __LINE__, __FUNCTION__,
                            pPktInfo->head.pktFlag, pPktInfo->head.offset, net_type, remote_ip, remote_port, pktQueue[pos].head.waiting);
                        pktQueue[pos].head.pktFlag = 0;
                        pktQueue[pos].head.totalLen = 0;
                        pktQueue[pos].head.rcvedLen=0;
                        pktQueue[pos].head.usedFlag = 1;                  
                        pktQueue[pos].head.waiting = 0; 
                        pktQueue[pos].head.waitCloseStatus = 0;
                        bit_clear(pktQueue[pos].head.bit);
                        pktQueue[pos].head.lenMap.clear();
                    } else {
                        packHead->msgType = UDP_RT_SPECIAL;
                        packHead->msgLen = sizeof(VerifyMsgHead)+sizeof(int);
                        *ptr = pktQueue[pos].head.waiting;
                        sendto_udpdata(listen_sid, ackBuff, packHead->msgLen, 0, pPktInfo->head.recvFromIp, pPktInfo->head.recvFromPort);
                        
                    }
                }
            }
        }
        pthread_testcancel();
    }      

    DEBUG_PRINT("thread_pktsend out.....\n");
    ELOG(L_INFO,"udp send thead outing(remote_socket_type=%d, remote_ip=0x%08X,remote_port=%u,socket_id=%d).....", 
        net_type, remote_ip,remote_port, send_sid);
    pthread_exit((void *) 0);
    return NULL;
}


udp_server_obj::udp_server_obj(ServerCfgInfo &cfg)
{
    memset(&confInfo,0, sizeof(ServerCfgInfo));
    memcpy(&confInfo,&cfg, sizeof(ServerCfgInfo));
    runFlag = 0;
    socketIds = NULL;
    pktQueue = NULL;
    pthreadpara = NULL;
    pthdrecvpara = NULL;
    thrId = 0;
    socket_id = -1;
    serverpktrecvthrd = 0;
    serverpktsendthrd = 0;
    shutdown = 0;
    pktFlagMaps.pktFlagmap.clear();
    pktFlagMaps.pid2sidmap.clear();
    
    for (int i=0; i<UDP_RECV_QUEUE_SIZE; i++) {
        UDPRecvDataQueues.PktBufStack[i] = NULL;
    }
    pthread_mutex_init(&(pktFlagMaps.mutex), NULL);
}
udp_server_obj::udp_server_obj(const udp_server_obj      &obj)
{
    memcpy(&confInfo,&(obj.confInfo), sizeof(ServerCfgInfo));
    socketIds = NULL;
    pktQueue = NULL;
    pthreadpara = NULL;
    pthdrecvpara = NULL;
    runFlag = 0;
    thrId = 0;
    socket_id = -1;
    serverpktrecvthrd = 0;
    serverpktsendthrd = 0;
    shutdown = 0;
    pktFlagMaps.pktFlagmap.clear();
    pktFlagMaps.pid2sidmap.clear();
    for (int i=0; i<UDP_RECV_QUEUE_SIZE; i++) {
        UDPRecvDataQueues.PktBufStack[i] = NULL;
    }
    pthread_mutex_init(&(pktFlagMaps.mutex), NULL);
}
udp_server_obj::~udp_server_obj()
{
    
    if(runFlag == 1) {
        DEBUG_PRINT("[%s][%d]:%s\n", __FILE__, __LINE__, __FUNCTION__);
        if(socket_id > -1) {
            close(socket_id);
            socket_id = -1;
        }       

        if(serverpktrecvthrd != 0) {
        //pthread_cancel(serverpktrecvthrd);
        pthread_join(serverpktrecvthrd,NULL);
        serverpktrecvthrd = 0;
        }
        
        if(serverpktsendthrd != 0) {
            //pthread_cancel(serverpktsendthrd);
            pthread_join(serverpktsendthrd,NULL);
            serverpktsendthrd = 0;
        }
        
        destroyRecvQueue(UDPRecvDataQueues);

        Pkt3Tuple pkt3Tuple;
        pkt3Tuple.ip = confInfo.listen_ip;
        pkt3Tuple.port = confInfo.listen_port;
        pkt3Tuple.protocol = NET_TYPE_UDP;
        sgg_RtpAuditRmv(pkt3Tuple);
    }

    //close(sendId);
    if (pthreadpara != NULL) {
        free(pthreadpara);
        pthreadpara = NULL;
    }
    if (pthdrecvpara != NULL) {
        free(pthdrecvpara);
        pthdrecvpara = NULL;
    }
    if(pktQueue != NULL) {
        DestroyPackQueue(&pktQueue, RECV_PKT_MIN_QUEUE);
        free(pktQueue);
        pktQueue = NULL;
    }
    
    

    if(socketIds != NULL) {
        for (unsigned i=0; i<confInfo.sgg_multi_lines; i++) {
            if (socketIds[i].socket_type == NET_TYPE_TCP) {
                close_tcpclient(socketIds[i].socket_id);
            } else if((socketIds[i].socket_type == NET_TYPE_UDP)) {
                close_udpclient(socketIds[i].socket_id);
            }

        }
        free(socketIds);
        socketIds = NULL;
    }
    
    pktFlagMaps.pktFlagmap.clear();
    pktFlagMaps.pid2sidmap.clear();
    pthread_mutex_destroy(&(pktFlagMaps.mutex));
    
    DEBUG_PRINT("[%s][%d]:%s\n", __FILE__, __LINE__, __FUNCTION__);
}

int udp_server_obj::init(void)
{
    int ret;
    runFlag = 0;
    if(socketIds == NULL) {
        socketIds = (SocketInfo*)malloc(sizeof(SocketInfo)*confInfo.sgg_multi_lines);
        if(socketIds==NULL) {
            goto end;
        }
    }
    memset(socketIds, 0, (sizeof(SocketInfo)*confInfo.sgg_multi_lines));
    
    for (unsigned i=0; i<confInfo.sgg_multi_lines; i++) {
        socketIds[i].socket_type = NET_TYPE_BUTT;
        socketIds[i].socket_id = -1;
        socketIds[i].lock = PTHREAD_MUTEX_INITIALIZER;
    }

    pthdrecvpara = (udp_thread_recv_para*)malloc(sizeof(udp_thread_recv_para));
    pthreadpara = (udp_thread_send_para*)malloc(sizeof(udp_thread_send_para));
    if (pthdrecvpara == NULL || pthreadpara == NULL) {
        goto end;
    }
    

    ret = InitOnePackQueue(&pktQueue);
    if(ret==-1) {
        DEBUG_PRINT("init recv packet queue error \n");
        return -1;
    } else {
        ELOG(L_INFO,"InitOnePackQueue successful");
    }
    pthread_rwlock_init(&rwlock,NULL);
    return 0;
    end:

    if(socketIds != NULL) {
        for (unsigned i=0; i<confInfo.sgg_multi_lines; i++) {
            pthread_mutex_destroy(&(socketIds[i].lock));
        }
        free(socketIds);
        socketIds = NULL;    
    }

    if(pthdrecvpara != NULL) {
        free(pthdrecvpara);
        pthdrecvpara = NULL;
    }

    if(pthreadpara != NULL) {
        free(pthreadpara);
        pthreadpara = NULL;
    }

    DestroyPackQueue(&pktQueue, RECV_PKT_MIN_QUEUE);
    
    return -1;
}

int udp_server_obj::getshutdown(void)
{
    int flag;
    pthread_rwlock_rdlock(&rwlock);
    flag = shutdown;
    pthread_rwlock_unlock(&rwlock);
    return flag;
}

void udp_server_obj::setshutdown(int flag)
{
    pthread_rwlock_wrlock(&rwlock);
    shutdown = flag;
    pthread_rwlock_unlock(&rwlock);
    return;
}



int udp_server_obj::request_queueDropedPkt(unsigned int pktFlag)
{
    /* 不支持udp心跳包则直接返回 */
    RET_IF(g_udpHbFlag==0, 0);

    int retValue = 0;
    int pos = GetPktQueuePos(pktQueue,pktFlag, false);
    //LOG_OF_DAY("request_queueDropedPkt ining...: sd=%d, pktFlag=%u, pos=%d\n", sd, pktFlag,pos);
    RET_IF(pos==-1, 0);
    ELOG(L_DEBUG, "request_queueDropedPkt: sd=%d, pktFlag=%u\n", socket_id, pktFlag);
    //retValue = bit_isAllSet(g_pktQueue[pos].head.bit, 0);
    //RET_IF(retValue==1, 0);
    unsigned int start = pktQueue[pos].head.waiting;
    unsigned int index = ((start+1)%UDP_PACKET_ORDER_LEN_MAX);
    int isAnyNoneSend = 0;
    std::queue<unsigned int> dropedPktIds;
    ELOG(L_DEBUG, "request_queueDropedPkt: index=%d, start=%u\n", index, start);
    dropedPktIds.push(start);
    while(index != start) {
        retValue = bit_get(pktQueue[pos].head.bit, index);
        if(retValue == 0) {
            dropedPktIds.push(index);
        } else {
            isAnyNoneSend++;
        }
        index = ((index+1)%UDP_PACKET_ORDER_LEN_MAX);
    }

    /* 此处说明缓存队列中存在丢包数据 */
    char ackBuff[512];//sizeof(VerifyMsgHead)+sizeof(int)*n
    VerifyMsgHead *packHead = (VerifyMsgHead*)ackBuff;
    unsigned int *ptr = (unsigned int*)(packHead+1);
    int size = dropedPktIds.size();
    //LOG_OF_DAY("request_queueDropedPkt running...: sd=%d, pktFlag=%u, pos=%d, size=%d, isAnyNoneSend=%d\n", sd, pktFlag,pos,size, isAnyNoneSend);
    if(isAnyNoneSend != 0) {
        while(!dropedPktIds.empty()) {
            *ptr = dropedPktIds.front();
            dropedPktIds.pop();
            ptr++;
        }
        packHead->magix=MAGIC_WORD_MAIN;
        packHead->version = 100;
        packHead->errCode = 0;
        packHead->msgType = UDP_RT_SPECIAL;
        packHead->msgLen = sizeof(VerifyMsgHead)+(sizeof(int)*size);
        //*ptr = pktFlag;
        sendto_udpdata(socket_id, ackBuff, packHead->msgLen, 0, pktQueue[pos].head.recvFromIp, pktQueue[pos].head.recvFromPort);
        ELOG(L_INFO,"pktFlag=%u, socket_id=%d, remote_ip=0x%08X,remote_port=%d, size=%d, isAnyNoneSend=%d", 
                            pktFlag, socket_id, pktQueue[pos].head.recvFromIp, pktQueue[pos].head.recvFromPort, size, isAnyNoneSend);
        ELOG(L_DEBUG, "request_queueDropedPkt: size=%u\n", size);
        ELOG(L_DEBUG, "request_queueDropedPkt: isAnyNoneSend=%u\n", isAnyNoneSend);
        return 1;
    }
    ELOG(L_DEBUG, "request_queueDropedPkt: outing\n");
    return 0;
}


void udp_server_obj::check_clientsocket(unsigned int sgg_multi_lines, unsigned int pos, unsigned int location) {
    struct tcp_info info; 
    int len=sizeof(info); 
    int sd = -1;
    int index = -1;
    unsigned int pktFlag = 0;
    int waiting = -1;
    std::list<unsigned int> pktFlagList;
    std::map<Pkt3Tuple,pktFlagWithOffset>::iterator it3Tupe2Pid;
    std::map<unsigned int,int>::iterator itPid2Sid;
    
    pktFlagList.clear();

    if (pos < sgg_multi_lines) {
        RET_VOID_IF(socketIds[pos].socket_type >= NET_TYPE_BUTT);
        if (socketIds[pos].socket_type == NET_TYPE_TCP) {
            sd = socketIds[pos].socket_id;
            getsockopt(sd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *)&len);
            if((sd<0) || (info.tcpi_state != TCP_ESTABLISHED)) {                              
                sd = run_tcpclient(socketIds[pos].remote_ip, socketIds[pos].remote_port, 0, true);
                RET_VOID_IF(sd < 0);
                //pthread_mutex_lock(&(mutex));
                close(socketIds[pos].socket_id);
                socketIds[pos].socket_id = sd;
                //pthread_mutex_unlock(&(mutex));
                return;
            }
        } 
        
    } else {
        for (unsigned i=0; i<sgg_multi_lines; i++) {
            pthread_mutex_lock(&(socketIds[i].lock));
            if(socketIds[i].socket_type >= NET_TYPE_BUTT) {
                pthread_mutex_unlock(&(socketIds[i].lock));
                continue;
            }
            sd = socketIds[i].socket_id;
            if (sd >= 0) {                
                socketIds[i].idleTimes++;
                if(socketIds[i].idleTimes > UDP_TUNNEL_IDLE_TIMES) {
                    socketIds[i].idleTimes = 0;
                    socketIds[i].idleCrcles++;
                    pktFlag = socketIds[i].pktFlag;
                    if(location == NET_LOCATION_SERVER) {
                        request_queueDropedPkt(pktFlag);
                    }
                    if(/*(1==retvalue) || */(socketIds[i].idleCrcles<UDP_TUNNEL_IDLE_CRCLES)) {
                        pthread_mutex_unlock(&(socketIds[i].lock));
                        continue;
                    }
                    
                    if (socketIds[i].socket_type == NET_TYPE_TCP) {
                        close_tcpclient(sd);
                    } else {
                        close_udpclient(sd);
                    }
                    index = GetPktQueuePos(pktQueue, pktFlag, false);  
                    if(index>=0) {
                        waiting = pktQueue[index].head.waiting;
                        pktQueue[index].head.pktFlag = 0;
                        pktQueue[index].head.totalLen = 0;
                        pktQueue[index].head.rcvedLen=0;
                        pktQueue[index].head.usedFlag = 1;                  
                        pktQueue[index].head.waiting = 0; 
                        bit_clear(pktQueue[index].head.bit);
                        pktQueue[index].head.lenMap.clear();                        
                    }
                    pktFlagList.push_back(pktFlag);
                    ELOG(L_INFO,"pktFlag=%u, remote_type[0:UDP,1:TCP]=%d, socket_id=%d, remote_ip=0x%08X,remote_port=%d,waiting=%d, index=%d",  
                        socketIds[i].pktFlag, socketIds[i].socket_type, socketIds[i].socket_id, socketIds[i].remote_ip, socketIds[i].remote_port, waiting, index);
                    socketIds[i].socket_id = -1;
                    socketIds[i].socket_type = NET_TYPE_BUTT;                    
                    
                    
                    
                }
                pthread_mutex_unlock(&(socketIds[i].lock));
                continue;
            }
            
            /*getsockopt(sd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *)&len);*/
            /*if((sd<0) || (info.tcpi_state != TCP_ESTABLISHED)) {*/ 
            //} 
            
            if (socketIds[i].socket_type == NET_TYPE_TCP) {
                sd = run_tcpclient(socketIds[i].remote_ip, socketIds[i].remote_port, 0,true);
            } else {
                sd = run_udpclient(socketIds[i].remote_ip, socketIds[i].remote_port, 0);
            }

            if(sd < 0) {
                pthread_mutex_unlock(&(socketIds[i].lock));
                continue;
            }
            socketIds[i].socket_id = sd;
            ELOG(L_INFO,"pktFlag=%u, remote_type[0:UDP,1:TCP]=%d, socket_id=%d, remote_ip=0x%08X,remote_port=%d", 
                        socketIds[i].pktFlag, socketIds[i].socket_type, socketIds[i].socket_id, socketIds[i].remote_ip, socketIds[i].remote_port);
            pthread_mutex_unlock(&(socketIds[i].lock));
        }

        pthread_mutex_lock(&(pktFlagMaps.mutex));
        for (std::list<unsigned int>::iterator it=pktFlagList.begin(); it!=pktFlagList.end(); ++it) {
            pktFlag = *it;
            DEBUG_PRINT("[%s][%d][%s]: pktFlag=%u\r\n", __FILE__, __LINE__, __FUNCTION__, pktFlag);
            itPid2Sid = pktFlagMaps.pid2sidmap.find(pktFlag);
            if(itPid2Sid != pktFlagMaps.pid2sidmap.end()) {
                pktFlagMaps.pid2sidmap.erase(itPid2Sid);
                DEBUG_PRINT("[%s][%d][%s]: clear pid2sid, pktFlag=%u\r\n", __FILE__, __LINE__, __FUNCTION__, pktFlag);
            }
            if (confInfo.location == NET_LOCATION_CLIENT) {
                for(it3Tupe2Pid = pktFlagMaps.pktFlagmap.begin(); it3Tupe2Pid!=pktFlagMaps.pktFlagmap.end(); ++it3Tupe2Pid) {
                    if(it3Tupe2Pid->second.pktFlag == pktFlag) {
                        pktFlagMaps.pktFlagmap.erase(it3Tupe2Pid);
                        DEBUG_PRINT("[%s][%d][%s]: clear 3tuple2pid, pktFlag=%u\r\n", __FILE__, __LINE__, __FUNCTION__, pktFlag);
                        break;
                    }
                }
            }
        }
        pthread_mutex_unlock(&(pktFlagMaps.mutex));
 
    }
    pktFlagList.clear();
    return;
}

int udp_server_obj::run()
{
    
    int retValue = 0;
    //threadpool_t *thrdPools = NULL;    
    
    //SggConfInfo *confInfo = (SggConfInfo*)arg;
    
    ELOG(L_INFO,"starting run udp server:ipaddr=0x%08X, port=%u, net_type=%u, location=%d", 
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location);
    DEBUG_PRINT("starting run udp server:ipaddr=0x%08X, port=%u, net_type=%u, location=%d\r\n",confInfo.listen_ip, 
        confInfo.listen_port, confInfo.listen_socket_type, confInfo.location);
    runFlag = 1;    
    socket_id = create_udpsocket(confInfo.listen_ip, confInfo.listen_port, UDP_PROTOCAL_BUFF_MAX_SIZE);    //创建监听socket
    if (socket_id < 0)
    {
        retValue =  -1;
        ELOG(L_DEBUG, "create udp socket failed:ip=0x%08x, port=%u, err=%d", confInfo.listen_ip, confInfo.listen_port, socket_id);
        goto end;
    }
    pthdrecvpara->location = confInfo.location;
    pthdrecvpara->listen_sid = socket_id;
    pthdrecvpara->pUDPRecvDataQueues = &UDPRecvDataQueues;
    pthdrecvpara->shutdown = 0;
    pthdrecvpara->listen_ip = confInfo.listen_ip;
    pthdrecvpara->listen_port = confInfo.listen_port;
    pthdrecvpara->sgg_multi_lines = confInfo.sgg_multi_lines;
    memcpy(pthdrecvpara->rtpCfgFlag, confInfo.rtpCfgFlag, sizeof(pthdrecvpara->rtpCfgFlag));

    pthreadpara->listen_sid = socket_id;
    pthreadpara->remote_socket_type = confInfo.remote_socket_type;
    pthreadpara->remote_ip = confInfo.remote_ip;
    pthreadpara->remote_port = confInfo.remote_port;
    pthreadpara->location = confInfo.location;
    pthreadpara->udp_send_interval = confInfo.udp_send_interval;
    pthreadpara->socketIds = socketIds;
    pthreadpara->pktQueue = pktQueue;
    pthreadpara->pUDPRecvDataQueues = &UDPRecvDataQueues;
    pthreadpara->shutdown = 0;
    pthreadpara->pktFlagMaps = &pktFlagMaps;
    pthreadpara->sgg_multi_lines = confInfo.sgg_multi_lines;
    retValue = createRecvQueue(UDPRecvDataQueues, confInfo.udp_buff_max_count);
    if (retValue < 0) {
        DEBUG_PRINT("Create recv buff queue error \n");
        retValue =  -ERR_UDP_SOCKET_ALLOC_MEM_FAILED;
        goto end;
    } 
    ELOG(L_INFO,"createRecvQueue successful");
    
    if (pthread_create(&serverpktrecvthrd, NULL, thread_udppktrecv, pthdrecvpara) != 0)//创建接收信息线程
    {
        DEBUG_PRINT("create pktrecv thread error:%s \n", strerror(errno));
        retValue = -ERR_UDP_SOCKET_CREATE_THREAD_FAILED;
        goto end;
    }
    //pthread_detach(serverpktrecvthrd);
    ELOG(L_INFO,"create thread_pktrecv successful");

    if (pthread_create(&serverpktsendthrd, NULL, thread_pktsend, pthreadpara) != 0) //创建组包上送线程
    {
        DEBUG_PRINT("create pktsend thread error:%s \n", strerror(errno));
        retValue = -ERR_UDP_SOCKET_CREATE_THREAD_FAILED;
        goto end;
    }
    //pthread_detach(serverpktsendthrd);
    ELOG(L_INFO,"create thread_pktsend successful");
    
    //pthread_join(g_serverpktsendthrd, NULL); 
    //pthread_join(g_serverpktrecvthrd, NULL);

    while (!getshutdown()) {
            check_clientsocket(confInfo.sgg_multi_lines, 0xffff, confInfo.location);
        sleep(1);
    }
    
end:   
    DEBUG_PRINT("[%s][%d]:[%s] \n", __FILE__, __LINE__, __FUNCTION__);
    if(socket_id > -1) {
        close(socket_id);
        socket_id = -1;
    }
    //close(sendId);

    pthdrecvpara->shutdown = 1;
    pthreadpara->shutdown = 1;

    if(serverpktrecvthrd != 0) {
        //pthread_cancel(serverpktrecvthrd);
        ELOG(L_DEBUG, "recv thread with pthread_join:ip=0x%08x, port=%u", confInfo.listen_ip, confInfo.listen_port);
        pthread_join(serverpktrecvthrd,NULL);
        serverpktrecvthrd = 0;
    }
    if(serverpktsendthrd != 0) {
        //pthread_cancel(serverpktsendthrd);
        ELOG(L_DEBUG, "send thread with pthread_join:ip=0x%08x, port=%u", confInfo.listen_ip, confInfo.listen_port);
        pthread_join(serverpktsendthrd,NULL);
        serverpktsendthrd = 0;
    }
    destroyRecvQueue(UDPRecvDataQueues);
    #if 0
    if (pthreadpara != NULL) {
        free(pthreadpara);
        pthreadpara = NULL;
    }
    if (pthdrecvpara != NULL) {
        free(pthdrecvpara);
        pthdrecvpara = NULL;
    }
    DestroyPackQueue(&pktQueue, RECV_PKT_MIN_QUEUE);
      
    #endif
    /* 删除UDP相关的审计信息 */
    Pkt3Tuple pkt3Tuple;
    pkt3Tuple.ip = confInfo.listen_ip;
    pkt3Tuple.port = confInfo.listen_port;
    pkt3Tuple.protocol = NET_TYPE_UDP;
    sgg_RtpAuditRmv(pkt3Tuple);
    //(void)threadpool_destroy(thrdPools);    
    DEBUG_PRINT("[%s][%d]: udp server socket exit:ipaddr=0x%08X, port=%u, net_type=%u, location=%d\n", __FILE__, __LINE__,
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location);
    ELOG(L_INFO,"udp server socket exit:ipaddr=0x%08X, port=%u, net_type=%u, location=%d", 
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location);
    //thrId=0;
    runFlag = 2;
    return -1;
    
}

void *udpSvrManageThrdProc(void *para)
{
    udp_server_obj *pobj = (udp_server_obj*)para; 
    //pthread_detach(pthread_self());
    pobj->run();    
    pthread_exit(NULL);
}

udp_server_manage::udp_server_manage(void*arg)
{
    if (arg != NULL) {
        memcpy(&confInfo, arg, sizeof(SggConfInfo));
    }
    pthread_rwlock_init(&rwlock,NULL);
    ELOG(L_INFO,"new obj with para");
}
udp_server_manage::udp_server_manage()
{
    pthread_rwlock_init(&rwlock,NULL);
    confInfo.location=NET_LOCATION_BUTT;
    ELOG(L_INFO,"new obj without para");
}


udp_server_manage::~udp_server_manage()
{
    //std::map<int,udp_server_obj>::iterator it;
    //udp_server_obj *obj = NULL;

    /*for (it=servers.begin(); it!=servers.end(); ++it) {
        obj = it->second;        
    }*/

    servers.clear();
    ELOG(L_INFO,"delete obj");
}

udp_server_manage* udp_server_manage::GetInstance()
{
    static udp_server_manage *instace = NULL;
    if(instace == NULL) {
        instace = new udp_server_manage;
    }
    return instace;
}

int udp_server_manage::init(void*arg)
{
    if(arg != NULL) {
        memcpy(&confInfo, arg, sizeof(SggConfInfo));
    }
    return 0;
}
int udp_server_manage::add(SvrObjCommInfo *commCfg, SvrObjBaseInfo*cfg)
{
    std::map<int,udp_server_obj>::iterator it;
    std::pair<std::map<int,udp_server_obj>::iterator,bool> ret;
    int retValue = 0;    
    ServerCfgInfo svrCfg;
    
    RET_IF((confInfo.location>=NET_LOCATION_BUTT || commCfg->location>=NET_LOCATION_BUTT), ERR_SGG_SERVER_M_NOT_READY);
    
    //svrCfg.location = confInfo.location;
    svrCfg.sgg_multi_lines = commCfg->sgg_multi_lines;
    svrCfg.tcp_buff_max_count = commCfg->tcp_buff_max_count;
    svrCfg.udp_buff_max_count = commCfg->udp_buff_max_count;
    svrCfg.udp_send_hb = commCfg->udp_send_heart_beat;
    svrCfg.udp_send_interval = commCfg->udp_send_interval;   
    
    svrCfg.location = commCfg->location;
    
    svrCfg.listen_ip = cfg->local.ip;
    svrCfg.listen_port = cfg->local.port;
    svrCfg.listen_socket_type = cfg->local.protocol;
    
    svrCfg.remote_ip = cfg->remote.ip;
    svrCfg.remote_port = cfg->remote.port;
    svrCfg.remote_socket_type = cfg->remote.protocol;
    memcpy(svrCfg.rtpCfgFlag, cfg->rtpCfgFlag, sizeof(svrCfg.rtpCfgFlag));
    
    pthread_rwlock_wrlock(&rwlock);    
    
    udp_server_obj obj(svrCfg);
    ret = servers.insert(std::pair<int,udp_server_obj>(svrCfg.listen_port, obj));
    
    if (ret.second!=false) {       
        ELOG(L_INFO,"add successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u, rtpCfgFlag=%u-%u-%u-%u)", 
            cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol,
            (int)cfg->rtpCfgFlag[0], (int)cfg->rtpCfgFlag[1], (int)cfg->rtpCfgFlag[2], (int)cfg->rtpCfgFlag[3]);
    } else {
        ELOG(L_WARNING,"obj existed(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
            cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
        pthread_rwlock_unlock(&rwlock);
        return ERR_UDP_SOCKET_SVR_PORT_EXIST;
    }
    
    it = servers.find(svrCfg.listen_port);
    if(it != servers.end()) {
        udp_server_obj *pobj = &(it->second);
        if((pobj->init() != 0) || ((retValue =pthread_create(&(pobj->thrId), NULL, udpSvrManageThrdProc, (void*)pobj))!=0)) {
            servers.erase(it);
            ELOG(L_ERROR,"start server object failed(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
        }  else {
            ELOG(L_INFO,"start server object successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",  
                cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
        }
    }
    
    pthread_rwlock_unlock(&rwlock);
    
    return 0;
}

int udp_server_manage::rmv(SvrObjBaseInfo*cfg)
{
    std::map<int,udp_server_obj>::iterator it;
    RET_IF(confInfo.location>=NET_LOCATION_BUTT, ERR_SGG_SERVER_M_NOT_READY);
    ELOG(L_INFO,"delete udp server object ining...(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                    cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
    pthread_rwlock_wrlock(&rwlock);
    it = servers.find(cfg->local.port);
    if(it != servers.end()) {
        udp_server_obj *pobj = &(it->second);
        if(pobj->thrId != 0) {
            int kill_rc = pthread_kill(pobj->thrId, 0);     //发送0号信号，测试是否存活
            if (kill_rc != ESRCH)  //线程存在
            {
                pobj->setshutdown(1);
                ELOG(L_INFO,"waiting delete tcp server object (listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                        cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
                pthread_join(pobj->thrId, NULL);
                pobj->thrId = 0;
                //pthread_cancel(pobj->thrId);
            }
        }
        servers.erase(it);
        ELOG(L_INFO,"delete udp server object successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                    cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);        
    }
    pthread_rwlock_unlock(&rwlock);
    return 0;
}

int udp_server_manage::rmv(SvrObjBaseInfo*cfg, int num)
{
    std::map<int,udp_server_obj>::iterator it;
    int i = 0;
    SvrObjBaseInfo *ptr = cfg;
    RET_IF(confInfo.location>=NET_LOCATION_BUTT, ERR_SGG_SERVER_M_NOT_READY);
    
    pthread_rwlock_wrlock(&rwlock);

    for(i=0; i<num; i++) {
        it = servers.find(ptr->local.port);
        if(it != servers.end()) {
            udp_server_obj *pobj = &(it->second);
            pobj->setshutdown(1);
        }
        ptr++;
    }

    ptr = cfg;
    for(i=0; i<num; i++) {
        ELOG(L_INFO,"delete udp server object ining...(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                    ptr->local.ip, ptr->local.port, ptr->local.protocol, ptr->remote.ip, ptr->remote.port, ptr->remote.protocol);
        it = servers.find(ptr->local.port);
        if(it != servers.end()) {
            udp_server_obj *pobj = &(it->second);
            if (pobj->thrId != 0) {

                ELOG(L_INFO,"waiting delete tcp server object (listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                        ptr->local.ip, ptr->local.port, ptr->local.protocol, ptr->remote.ip, ptr->remote.port, ptr->remote.protocol);  
                pthread_join(pobj->thrId, NULL);
                pobj->thrId = 0;

            }
            servers.erase(it);
            ELOG(L_INFO,"delete udp server object successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",  
                        ptr->local.ip, ptr->local.port, ptr->local.protocol, ptr->remote.ip, ptr->remote.port, ptr->remote.protocol);
        }
        
        ptr++;
    }
    
    pthread_rwlock_unlock(&rwlock);
    return 0;
}





int udp_server_manage::run()
{
    ServerCfgInfo svrCfg;
    int ret = 0;
    std::map<int,udp_server_obj>::iterator it;
    udp_server_obj *pobj = NULL;

    if(confInfo.listen_port_begin>confInfo.listen_port_end ||
        confInfo.remote_port_begin>confInfo.remote_port_end ||
        ((confInfo.listen_port_end-confInfo.listen_port_begin)!=(confInfo.remote_port_end-confInfo.remote_port_begin))) {
        ELOG(L_ERROR,"para error: listen_port_begin=%d, listen_port_end=%d, remote_port_begin=%d, confInfo.remote_port_end=%d.",
                    confInfo.listen_port_begin, confInfo.listen_port_end, confInfo.remote_port_begin, confInfo.remote_port_end); 
        return -1;
    } 

    svrCfg.listen_ip = confInfo.listen_ip;
    svrCfg.listen_socket_type = confInfo.listen_socket_type;
    svrCfg.location = confInfo.location;
    svrCfg.remote_ip = confInfo.remote_ip;
    svrCfg.remote_socket_type = confInfo.remote_socket_type;
    svrCfg.sgg_multi_lines = confInfo.sgg_multi_lines;
    svrCfg.tcp_buff_max_count = confInfo.tcp_buff_max_count;
    svrCfg.udp_buff_max_count = confInfo.udp_buff_max_count;
    svrCfg.udp_send_hb = confInfo.udp_send_hb;
    svrCfg.udp_send_interval = confInfo.udp_send_interval;

    pthread_rwlock_wrlock(&rwlock);
    for(unsigned int port=confInfo.listen_port_begin,rport=confInfo.remote_port_begin; ((port<=confInfo.listen_port_end)&&(port>0));port++, rport++) {
        svrCfg.listen_port=port;
        svrCfg.remote_port=rport;
        udp_server_obj obj(svrCfg);
        servers.insert(std::pair<int,udp_server_obj>(port, obj));
    }
    pthread_rwlock_unlock(&rwlock);

    pthread_rwlock_rdlock(&rwlock);
    for (it=servers.begin(); it!=servers.end(); ++it) {
        pobj = &(it->second);
        if((pobj->init() != 0) || ((ret=pthread_create(&(pobj->thrId), NULL, udpSvrManageThrdProc, (void*)pobj))!=0)) {
            pthread_rwlock_unlock(&rwlock);
            goto end;
        }        
    }
    pthread_rwlock_unlock(&rwlock);
    ELOG(L_INFO,"run udp server successful");

    while(!g_stopflag) {
        sleep(30);
        pthread_rwlock_rdlock(&rwlock);
        for (it=servers.begin(); it!=servers.end(); ++it) {
            pobj = &(it->second);
            if (pobj->runFlag == 2) {
                if(pobj->thrId != 0) {
                    ELOG(L_INFO,"Reclaim udp server thread resources (listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                        pobj->confInfo.listen_ip, pobj->confInfo.listen_port, pobj->confInfo.listen_socket_type, pobj->confInfo.remote_ip, pobj->confInfo.remote_port, pobj->confInfo.remote_socket_type);  
                    pthread_join(pobj->thrId, NULL);
                    pobj->thrId = 0;
                }
                
                ret=pthread_create(&(pobj->thrId), NULL, udpSvrManageThrdProc, (void*)pobj);
                ELOG(L_INFO,"re_run udp server(port=%d)", it->first);
            }
                   
        }
        pthread_rwlock_unlock(&rwlock);
    }
    return 0;

end:
    for (it=servers.begin(); it!=servers.end(); ++it) {
        pobj = &(it->second);
        if(pobj->thrId != 0) {
            int kill_rc = pthread_kill(pobj->thrId, 0);     //发送0号信号，测试是否存活
            if (kill_rc != ESRCH)  //线程存在
            {
                pobj->setshutdown(1);
            }
        }
    }
    sleep(5);    
    servers.clear();
    ELOG(L_INFO,"run udp server failed(errCode=%d)", ret);
    return -1;
        

}



