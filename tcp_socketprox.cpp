#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <map>
#include "utils.h"
#include "log.h"
#include "threadpool.h"
#include "sgg_err.h"
#include "sgg_manage.h"
#include "sgg_stack.h"
#include "tcp_socket.h"
#include "tcp_socketprox.h"


typedef struct {
    int usedflag;
    int server_sid;
    int client_sid;
    int client_sid2;
    int client_closewaiting;
    int location;    
    int flag; /* 申请的线程池组标记 */
    unsigned int pktid;
    unsigned char exitFlag[4];/*四个方向（上行读写、下行读写）0表示正在运行未退出，1表示已经退出 */
    pthread_mutex_t lock;
    
    int sgg_mode; //SGG_MODE_BUTT
    int remote_socket_type;
    unsigned int remote_ip;
    unsigned int remote_port;
    int dremote_socket_type;
    unsigned int dremote_ip;
    unsigned int dremote_port;
    unsigned int pktdir[4];
    pthread_rwlock_t rwlock;
    int stopflag; 
    TCPRecvDataQueue queue[PACKET_DIRECTION_BUTT];
}tcpprox_thread_para;

extern int g_stopflag;

int addTcpProxDetectPkt(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int &isFirstPkt) {
    SendPackStr *sendpkt = (SendPackStr*)pbuf;

    if (isFirstPkt == 0) {
        return 0;
    }
    sendpkt->head.offset = offset++;
    sendpkt->head.pktTotalLen = 0;
    sendpkt->head.buf_size = sizeof(int);
    sendpkt->head.pktFlag = pktid;
    sendpkt->head.magicWord = MAGIC_WORD_DETECT;
    isFirstPkt = 0;
    return 1;
}


int recvTcpProxUpDataPre(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int flag) {
    SendPackStr *sendpkt = (SendPackStr*)pbuf;
    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    if (isfinished == 1) {
        return 0;
    }
    sendpkt->head.offset = offset++;
    sendpkt->head.pktTotalLen = 0;
    sendpkt->head.buf_size = 0;
    sendpkt->head.pktFlag = pktid;
    sendpkt->head.magicWord = MAGIC_WORD_TAIL;
    isfinished = 1;
    return -1;
}


int recvTcpProxUpData(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int &isFirstPkt, int thread_flag) {
    int rv = 0;
    int retValue = 0;
    SendPackStr *sendpkt = (SendPackStr*)pbuf;

    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    RET_IF((isfinished == 1), retValue);

    if (location == NET_LOCATION_SERVER && isFirstPkt == 1) {
        rv = recv(sd, (void*)sendpkt, buf_len, 0);
    } else {
        rv = recv(sd, sendpkt->buf, buf_len-sizeof(UserHeadInfo), 0);
    }    
    
    if (rv <= 0) {
        /* 非阻塞tcp    socket正常连接场景 */
        if((rv < 0 && (errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN))) {
            /* 需要回退申请的pbuf */
            retValue = 0;
        } else { /*非阻塞式tcp socket退出场景后处理*/
            DEBUG_PRINT("[recvTcpProxUpData] recv ended[pktdir=%d]:%s \n", pktdir, strerror(errno));
            ELOG(L_INFO,"recv ended, flag=%d, pktdir=%u, pktid=%u, size=%d, location=%d, rv=%d, sd=%d, errno=%d, str:%s", 
                thread_flag, pktdir, pktid, sendpkt->head.buf_size, location, rv, sd, errno, strerror(errno));
            /* 发送结束尾包 */
            sendpkt->head.offset = offset++;
            sendpkt->head.pktTotalLen = 0;
            sendpkt->head.buf_size = 0;
            sendpkt->head.pktFlag = pktid;
            sendpkt->head.magicWord = MAGIC_WORD_TAIL;
            isfinished = 1;
            retValue = -1;
        }
    } else {
        if (location == NET_LOCATION_SERVER && isFirstPkt == 1) {
            pktid = sendpkt->head.pktFlag;
            sendpkt->head.buf_size = rv - sizeof(UserHeadInfo);
            sendpkt->head.pktTotalLen = rv - sizeof(UserHeadInfo);
            isFirstPkt = 0;
            ELOG(L_INFO,"flag=%d, pktdir=%u, pktid=%u, socket_type=%d, location=%d, size=%d.", 
                    thread_flag, pktdir, sendpkt->head.pktFlag, remote_socket_type, location, rv);
        } else {
            sendpkt->head.magicWord = MAGIC_WORD_MAIN;
            sendpkt->head.pktFlag = pktid;            
            sendpkt->head.buf_size = rv;
            sendpkt->head.pktTotalLen = rv;
            if (location == NET_LOCATION_CLIENT && isFirstPkt == 1) {
                isFirstPkt = 0;
                ELOG(L_INFO,"flag=%d, pktdir=%u, pktid=%u, socket_type=%d, location=%d, size=%d.", 
                    thread_flag, pktdir, sendpkt->head.pktFlag, remote_socket_type, location, rv);                
            }
        }
        sendpkt->head.offset = offset++;        
        DEBUG_PRINT("[%s]pktdir=%d, size=%d:%s\r\n", __FUNCTION__, pktdir, sendpkt->head.buf_size, sendpkt->buf); //输出接受到内容
        retValue = 1;
    }        
    return retValue;
}

int recvTcpProxDownDataPre(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                                unsigned int pktdir, int remote_socket_type, int location, int &isFirstPkt, int flag) {
    SendPackStr *sendpkt = (SendPackStr*)pbuf;
    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    if (isfinished == 1) {
        return 0;
    }
    sendpkt->head.offset = offset++;
    sendpkt->head.pktTotalLen = 0;
    sendpkt->head.buf_size = 0;
    sendpkt->head.pktFlag = pktid;
    sendpkt->head.magicWord = MAGIC_WORD_TAIL;
    isfinished = 1;
    return -1;
}

int recvTcpProxDownData(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int &isFirstPkt, int thread_flag) {
    int rv = 0;
    int retValue = 0;
    SendPackStr *sendpkt = (SendPackStr*)pbuf;

    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    if (isfinished == 1) {
        return retValue;
    }
    if (location == NET_LOCATION_CLIENT && isFirstPkt == 1) {
        rv = recv(sd, (void*)sendpkt, buf_len, 0);
    } else {
        rv = recv(sd, sendpkt->buf, buf_len-sizeof(UserHeadInfo), 0);
    }
    
    if (rv <= 0) {
        /* 非阻塞tcp    socket正常连接场景 */
        if((rv < 0 && (errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN))) {
            /* 需要回退申请的pbuf */
            retValue = 0;
        } else { /*非阻塞式tcp socket退出场景后处理*/
            DEBUG_PRINT("[recvTcpProxDownData] recv ended[pktdir=%d]:%s \n", pktdir, strerror(errno));
            ELOG(L_INFO,"recv ended, flag=%d, pktdir=%u, pktid=%u, size=%d, location=%d, rv=%d, sd=%d, errno=%d, str:%s", 
                thread_flag, pktdir, pktid, sendpkt->head.buf_size, location, rv, sd, errno, strerror(errno));
            /* 发送结束尾包 */
            sendpkt->head.offset = offset++;
            sendpkt->head.pktTotalLen = 0;
            sendpkt->head.buf_size = 0;
            sendpkt->head.pktFlag = pktid;
            sendpkt->head.magicWord = MAGIC_WORD_TAIL;
            isfinished = 1;
            retValue = -1;
        }
    } else {
        if (location == NET_LOCATION_CLIENT && isFirstPkt == 1) {
            pktid = sendpkt->head.pktFlag;
            sendpkt->head.buf_size = rv - sizeof(UserHeadInfo);
            sendpkt->head.pktTotalLen = rv - sizeof(UserHeadInfo);
            isFirstPkt = 0;
            ELOG(L_INFO,"flag=%d, pktdir=%u, pktid=%u, size=%d, location=%d,sd=%d.", 
                    thread_flag, pktdir, sendpkt->head.pktFlag, sendpkt->head.buf_size, location, sd);
        } else {
            sendpkt->head.magicWord = MAGIC_WORD_MAIN;
            sendpkt->head.pktFlag = pktid;            
            sendpkt->head.buf_size = rv;
            sendpkt->head.pktTotalLen = rv;
        }
        sendpkt->head.offset = offset++;
        DEBUG_PRINT("[%s]pktdir=%d, size=%d:%s\r\n", __FUNCTION__, pktdir, rv, sendpkt->buf); //输出接受到内容
        retValue = 1;
    }        
    return retValue;
}                        


void *TcpProxWriteTask(void *arg1, void *arg2, void*arg3)
{
    tcpprox_thread_para* para = (tcpprox_thread_para*)arg1;
    int location = para->location;
    int remote_socket_type = para->remote_socket_type;    
    int thread_flag = para->flag;
    unsigned int remote_ip = para->remote_ip;
    int remote_port = para->remote_port;
    int wpos, retValue=0;
    int sd = para->server_sid;
    char *pbuf = NULL;
    unsigned int offset = 0;    
    unsigned int pktid = para->pktid;
    int isfinished = 0;
    int isTimeoutProc = 0;
    unsigned int pktdir = *((unsigned int*)arg2);
    int isFirstPkt = 1;
    int csd = -1;
    THandle handle = NULL;
    
    DEBUG_PRINT("[%s] ining... :flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n", __FUNCTION__,
        thread_flag,pktdir,pktid, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    ELOG(L_INFO,"TcpProxWriteTask ining... : flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.", 
        thread_flag, pktdir, pktid, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    if (pktdir >= PACKET_DIRECTION_BUTT) {
        goto end;
    }

    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();    
    
    if (location==NET_LOCATION_CLIENT && pktdir == PACKET_DIRECTION_UP) {
        insetSocketIdByPktId(pktid, sd);
    }
    
    while(!g_stopflag) {
        wpos = para->queue[pktdir].wpos; 
        if(PushStack(para->queue[pktdir].PktBufStack[wpos], &pbuf)==-1) {
            if (wpos == para->queue[pktdir].rpos) {
                pthread_mutex_lock(&(para->queue[pktdir].lock));
                pthread_cond_signal(&(para->queue[pktdir].cond));
                para->queue[pktdir].wpos = ((++wpos)%TCP_RECV_QUEUE_MAX_COUNT);
                pthread_mutex_unlock(&(para->queue[pktdir].lock));
                BREAK_IF(isfinished == 1);
            } else {
                ELOG(L_WARNING,"queue is full... : flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, socket_type=%d, location=%d.",
                    thread_flag, pktdir, pktid, para->server_sid, para->client_sid, remote_socket_type, location);
                usleep(50);
            }
            continue;
        }

        if (pktdir == PACKET_DIRECTION_UP) {
            if (location == NET_LOCATION_CLIENT) {
                /* if分支主要是处理多并发场景下，下行的发送端结束后释放上行server_sid后，该server_id被系统立即分配出去，导致上行错误的接收到新的流上的数据*/
                if (para->server_sid != getSocketIdByPktId(pktid)) {
                    retValue = recvTcpProxUpDataPre(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                    pktdir, remote_socket_type, location, thread_flag);
                } else {
                    if(isFirstPkt == 1) {
                        retValue = addTcpProxDetectPkt(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                    pktdir, remote_socket_type, location, isFirstPkt);
                    } else {
                        retValue = recvTcpProxUpData(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                        pktdir, remote_socket_type, location, isFirstPkt,thread_flag);
                    }
                }
            } else {
                if (para->stopflag == 1) {
                    retValue = recvTcpProxUpDataPre(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                    pktdir, remote_socket_type, location, thread_flag);
                } else {
                    retValue = recvTcpProxUpData(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                            pktdir, remote_socket_type, location, isFirstPkt,thread_flag);
                    para->pktid = pktid;
                }
                
            }
        } else {
            
            if (location == NET_LOCATION_SERVER && para->client_closewaiting==1) {
                retValue = recvTcpProxDownDataPre(sd, pbuf, PER_PACKET_MAX_SIZE, offset, para->pktid, isfinished, 
                                pktdir, remote_socket_type, location, isFirstPkt, thread_flag);
            } else {
                
                sd = (location==NET_LOCATION_CLIENT)?para->server_sid:para->client_sid;
                if(sd<0) {
                    BackPushStack(para->queue[pktdir].PktBufStack[wpos]);
                    continue;
                }
            
                retValue = recvTcpProxDownData(sd, pbuf, PER_PACKET_MAX_SIZE, offset, para->pktid, isfinished, 
                                pktdir, remote_socket_type, location, isFirstPkt, thread_flag);
                if (location == NET_LOCATION_SERVER) {
                    if(retValue == 0) {
                        addTimer(&handle, 30000000);
                        if (isTimerOut(handle)) {
                            if(isTimeoutProc==0) {
                                recvTcpProxDownDataPre(sd, pbuf, PER_PACKET_MAX_SIZE, offset, para->pktid, isfinished, 
                                    pktdir, remote_socket_type, location, isFirstPkt, thread_flag);
                                ELOG(L_WARNING, "recv timeout... : flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, socket_type=%d, location=%d.", 
                                    thread_flag,pktdir, pktid, para->server_sid, para->client_sid, remote_socket_type, location);
                                isTimeoutProc = 1;
                                continue;
                            }
                        }
                    } else {
                        delTimer(&handle);
                        isTimeoutProc = 0;
                    }
                }

            }
        }

        EXEC_IF((retValue == 0), BackPushStack(para->queue[pktdir].PktBufStack[wpos]));
        
        if (wpos == para->queue[pktdir].rpos && !IsStackEmpty(para->queue[pktdir].PktBufStack[wpos])) {
            pthread_mutex_lock(&(para->queue[pktdir].lock));
            pthread_cond_signal(&(para->queue[pktdir].cond));
            para->queue[pktdir].wpos = ((++wpos)%TCP_RECV_QUEUE_MAX_COUNT);
            pthread_mutex_unlock(&(para->queue[pktdir].lock));
            BREAK_IF(isfinished == 1);
        }
    }  
    
end:
    /* 写任务退出不是由于正常的socket接收结束而退出，正常接收结束退出会发送结束标记给读任务，唤醒读任务 */
    if (isfinished != 1) {
        pthread_mutex_lock(&(para->queue[pktdir].lock));
        pthread_cond_signal(&(para->queue[pktdir].cond));
        pthread_mutex_unlock(&(para->queue[pktdir].lock));
        ELOG(L_INFO,"TcpProxWriteTask exiting... : flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, socket_type=%d, location=%d.",
            thread_flag,pktdir, pktid, para->server_sid, para->client_sid, remote_socket_type, location);
    }

    if (location == NET_LOCATION_CLIENT) {
        csd = para->server_sid;
        if (pktdir == PACKET_DIRECTION_DOWN) {
            csd = para->server_sid;
            close(para->server_sid);
            para->server_sid = -1;
        } else {
            /* 此处表明上行方向没有发送一个数据包出去，此时需要直接关闭accept的socket                    id */
            if(isFirstPkt == 1) {
                csd = para->server_sid;
                close(para->server_sid);
                para->server_sid = -2;
            }
        }

    } else {
        if (pktdir == PACKET_DIRECTION_UP) {
            csd = para->server_sid;
            close(para->server_sid);
            para->server_sid = -1;
            /* 考虑代理主要为http代理，上行方向提前关闭上行socket，下行方向由服务器端主动关闭 */
            csd = para->client_sid;
            //close(para->client_sid);

            /* 上行方向没有读取到一个有效的数据，此时需要通知下行方向读取线程退出 */
            if (isFirstPkt == 1) {
                para->client_closewaiting = 1;
            }
        } else {
            /* 2020/04/03: 此处将client_sid置为-1不合理，原因是当下行方向提前结束的时候，上行方向还有结束，此时上行方向直接申请新的sid，导致老的sid在协议栈里面残留 */
            /* 下行方向只修改sid值 */            
            //para->client_sid = -1;
            close(para->client_sid);
        }    
    }   

    if(pktdir < PACKET_DIRECTION_BUTT) {
        pthread_mutex_lock(&(para->lock));
        para->exitFlag[pktdir*PACKET_DIRECTION_BUTT] = 1;
        pthread_mutex_unlock(&(para->lock));
    }
    delTimer(&handle);
    DEBUG_PRINT("[%s] outing... : flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n", __FUNCTION__,
        thread_flag,pktdir,pktid, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    ELOG(L_INFO,"TcpProxWriteTask outing... : flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d, csd=%d.", 
        thread_flag,pktdir, pktid, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location,csd);
    return NULL;

}

void *TcpProxReadTask(void *arg1, void *arg2, void*arg3)
{
    tcpprox_thread_para* para = (tcpprox_thread_para*)arg1;
    int location = para->location;
    int remote_socket_type = para->remote_socket_type;
    unsigned int remote_ip = para->remote_ip;
    unsigned int remote_port = para->remote_port;
    int thread_flag = para->flag;
    unsigned int pktid = 0;
    int retValue = 0;
    unsigned int pktdir = *((unsigned int*)arg2);
    int rpos=-1;
    int csd=-1;
    char *pbuf;
    SendPackStr *sendpkt = NULL;
    int isfirstpkt = 1;
    int failedtimes = 0;
    int count = 0;
    unsigned int poffset = 0;
   
    
    DEBUG_PRINT("[%s] ining...: flag=%d, pktdir=%u,remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n", __FUNCTION__,
        thread_flag, pktdir, remote_ip, remote_port,remote_socket_type, location);
    ELOG(L_INFO,"TcpProxReadTask ining...: flag=%d, pktdir=%u, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.", 
        thread_flag, pktdir, remote_ip, remote_port,remote_socket_type, location);

    if (pktdir >= PACKET_DIRECTION_BUTT) {
        goto end;
    }
    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();
    
    while (!g_stopflag) {
        rpos = para->queue[pktdir].rpos;
        if (IsStackEmpty(para->queue[pktdir].PktBufStack[rpos])) {
            pthread_mutex_lock(&(para->queue[pktdir].lock));
            para->queue[pktdir].rpos = para->queue[pktdir].wpos;
            pthread_cond_wait(&(para->queue[pktdir].cond), &(para->queue[pktdir].lock));
            rpos = para->queue[pktdir].rpos;
            pthread_mutex_unlock(&(para->queue[pktdir].lock));            
        }
        retValue = PopStack(para->queue[pktdir].PktBufStack[rpos], &pbuf); 
        if (retValue != -1) {
            sendpkt = (SendPackStr*)pbuf;
            BREAK_IF(sendpkt->head.magicWord == MAGIC_WORD_TAIL);
            CONTINUE_IF(sendpkt->head.buf_size == 0);
            if (pktdir == PACKET_DIRECTION_UP) {
                poffset = 0;
                checkTcpClientSocketAlive(remote_socket_type, thread_flag, pktdir, remote_ip, remote_port, para->client_sid);
                if(sendpkt->head.magicWord == MAGIC_WORD_DETECT && location == NET_LOCATION_SERVER) {
                    poffset = sizeof(int);
                    if(sendpkt->head.buf_size<=poffset) {    
                        continue;
                    }                    
                }
                if ((location == NET_LOCATION_SERVER) || (isfirstpkt != 1)) {
                    retValue = send_tcpdata(para->client_sid, sendpkt->buf+poffset, (sendpkt->head.buf_size-poffset));                    
                    DEBUG_PRINT("[TcpProxReadTask_1] flag=%d, isfirstpkt=%d, sd=%d, pktid=%u, len=%d, retValue=%d\n", thread_flag,isfirstpkt, para->client_sid, sendpkt->head.pktFlag, sendpkt->head.buf_size, retValue);
                } else {
                    retValue = send_tcpdata(para->client_sid, pbuf, sendpkt->head.buf_size+sizeof(UserHeadInfo));
                    DEBUG_PRINT("[TcpProxReadTask_2] flag=%d, isfirstpkt=%d, sd=%d, pktid=%u, len=%d, retValue=%d\n", thread_flag,isfirstpkt, para->client_sid, sendpkt->head.pktFlag, sendpkt->head.buf_size, retValue);
                    isfirstpkt = 0;
                }
                
            } else {
                if (location == NET_LOCATION_CLIENT) {
                    if (isfirstpkt == 1) {
                        para->client_sid = getSocketIdByPktId(sendpkt->head.pktFlag);
                        pktid = sendpkt->head.pktFlag;
                        //isfirstpkt = 0;
                        
                    }
                    retValue = send_tcpdata(para->client_sid, sendpkt->buf, sendpkt->head.buf_size);
                    EXEC_IF((isfirstpkt == 1), isfirstpkt = 0);
                    DEBUG_PRINT("[TcpProxReadTask_3] flag=%d, isfirstpkt=%d, sd=%d, pktid=%u, len=%d, retValue=%d\n", thread_flag,isfirstpkt, para->client_sid, sendpkt->head.pktFlag, sendpkt->head.buf_size, retValue);
                } else {
                    checkTcpClientSocketAlive(remote_socket_type, thread_flag, pktdir,para->dremote_ip, para->dremote_port, para->client_sid2, g_stopflag);
                    if (isfirstpkt == 1) {
                        retValue = send_tcpdata(para->client_sid2, (char*)sendpkt, sendpkt->head.buf_size+sizeof(UserHeadInfo));
                        //pktid = sendpkt->head.pktFlag;
                        DEBUG_PRINT("[TcpProxReadTask_4] flag=%d, isfirstpkt=%d, sd=%d, pktid=%u, len=%d, retValue=%d\n", thread_flag,isfirstpkt, para->client_sid2, sendpkt->head.pktFlag, sendpkt->head.buf_size, retValue);
                        isfirstpkt = 0;
                    } else {
                        retValue = send_tcpdata(para->client_sid2, sendpkt->buf, sendpkt->head.buf_size);
                        DEBUG_PRINT("[TcpProxReadTask_5] flag=%d, isfirstpkt=%d, sd=%d, pktid=%u, len=%d, retValue=%d\n", thread_flag,isfirstpkt, para->client_sid2, sendpkt->head.pktFlag, sendpkt->head.buf_size, retValue);
                    }
                }
            }
            /* 此处进行保护，即尝试调用send接口发送3次，仍然发送失败，则不进行回退，该报文直接丢弃 */
            if ((retValue < 0) && ((++failedtimes) <= 3)) {
                BackPopStack(para->queue[pktdir].PktBufStack[rpos]);
            } else {
                failedtimes = 0;
            }            
        } 
        
    }

end: 

    if (location == NET_LOCATION_CLIENT) {
        if (pktdir == PACKET_DIRECTION_DOWN) {
            deleteSocketIdByPktId(pktid);
            para->stopflag = 1;
        } 
        csd = para->client_sid;
        close(para->client_sid);
        para->client_sid = -3;   
        //para->usedflag = 0;
    } else {
        if (pktdir == PACKET_DIRECTION_DOWN) {
            csd = para->client_sid2;
            close(para->client_sid2);
            para->client_sid2 = -1;  
            //para->usedflag = 0;
            para->stopflag = 1;
        } else {
            /* server端上行方向发送报文socketid关闭在下行接收端接收结束后关闭，如果此处关闭，可能导致无法接收对端server的返回报文 */
            //close(para->client_sid);
            //para->client_sid = -1;  
        }
    }    

    if(pktdir < PACKET_DIRECTION_BUTT) {
        count = 0;
        do {
            pthread_mutex_lock(&(para->lock));
            if(para->exitFlag[pktdir*PACKET_DIRECTION_BUTT] == 1) {
                para->exitFlag[pktdir*PACKET_DIRECTION_BUTT+1] = 1;
                destroyTcpRecvQueue(para->queue[pktdir], TCP_RECV_QUEUE_MAX_COUNT);
                pthread_mutex_unlock(&(para->lock));
                break;
            }
            pthread_mutex_unlock(&(para->lock));
            if(++count>5000) {
                ELOG(L_WARNING,"TcpProxReadTask timeout...: flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d, rpos=%d.",  
                    thread_flag,pktdir, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location, rpos);
                count = 0;
            }
            usleep(1000);
        }while(1);
    }
    
    /*if(pktdir<PACKET_DIRECTION_BUTT) {
        destroyTcpRecvQueue(para->queue[pktdir], TCP_RECV_QUEUE_MAX_COUNT);
    }*/

    EXEC_IF(((location == NET_LOCATION_CLIENT) || ((location == NET_LOCATION_SERVER) && (pktdir == PACKET_DIRECTION_DOWN))), (para->usedflag = 0));
    
    DEBUG_PRINT("[%s] outing...: flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n", __FUNCTION__,
        thread_flag,pktdir, para->pktid, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    ELOG(L_INFO,"TcpProxReadTask outing...: flag=%d, pktdir=%u, pktid=%u, server_sid=%d, client_sid=%d, remote_ip=0x%8X, remote_port=%d,socket_type=%d, location=%d, rpos=%d, csd=%d.", 
        thread_flag,pktdir, para->pktid, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location, rpos,csd);
    return NULL;

}

void* run_tcpproxserver(void*arg) {

    threadpool_t *thrdPools = NULL;
    int dir = *((int*)arg);
    SggConfInfo *confInfo = getSggConfInfo();
    int retValue = 0;
    int socket_id=-1, accept_st=-1;
    tcpprox_thread_para *thrdpara = NULL;
    unsigned int g_pktid = 0;
    int index = -1;
    unsigned int i;
    try 
    {
        if (dir >= PACKET_DIRECTION_BUTT) {
            ELOG(L_ERROR," para is invalid:dir=%d", dir);
            return NULL;
        }
        thrdpara = (tcpprox_thread_para*)malloc(sizeof(tcpprox_thread_para)*(confInfo->sgg_multi_lines));
        if (thrdpara == NULL) {
            ELOG(L_ERROR,"the tcp socket id of send is invalid");
            DEBUG_PRINT("the tcp socket id of send is invalid\r\n");
            return NULL;
        }
        memset(thrdpara, 0, sizeof(tcpprox_thread_para)*(confInfo->sgg_multi_lines));
        for (i=0; i<confInfo->sgg_multi_lines;i++) {
            pthread_cond_init(&(thrdpara[i].queue[PACKET_DIRECTION_UP].cond), NULL);
            pthread_cond_init(&(thrdpara[i].queue[PACKET_DIRECTION_DOWN].cond), NULL);
            pthread_mutex_init(&(thrdpara[i].queue[PACKET_DIRECTION_UP].lock), NULL);
            pthread_mutex_init(&(thrdpara[i].queue[PACKET_DIRECTION_DOWN].lock), NULL);
            pthread_mutex_init(&(thrdpara[i].lock), NULL);
        }
        

        if (dir == PACKET_DIRECTION_UP) {
            socket_id = create_tcplisten(confInfo->listen_ip, confInfo->listen_port, TCP_PROX_PROTOCAL_BUFF_MAX_SIZE); //创建监听socket
        } else {
            socket_id = create_tcplisten(confInfo->dlisten_ip, confInfo->dlisten_port, TCP_PROX_PROTOCAL_BUFF_MAX_SIZE); //创建监听socket
        }
        
        if (socket_id < 0) {
            retValue =  -1;
            throw retValue;
        }
    
        thrdPools = threadpool_create(confInfo->sgg_multi_lines*2, confInfo->sgg_multi_lines*4, confInfo->sgg_multi_lines*4);
        if (thrdPools == NULL) {
            ELOG(L_ERROR,"create thread pools failed");
            DEBUG_PRINT("create thread pools failed\r\n");
            retValue = -1;
            throw retValue;
        }
        
        do {
            accept_st = accept_socket(socket_id,true); //获取连接的的socket
            do { 
                index = ((index+1)%(confInfo->sgg_multi_lines));
                BREAK_IF(thrdpara[index].usedflag == 0);                
            }while(1);
            thrdpara[index].pktdir[2*dir] = dir;
            thrdpara[index].pktdir[2*dir+1] = dir;            
            thrdpara[index].client_sid = -3;
            thrdpara[index].client_sid2 = -3;
            thrdpara[index].server_sid = accept_st;
            thrdpara[index].location = confInfo->location;
            if (dir == PACKET_DIRECTION_UP) {
                thrdpara[index].pktid = ++g_pktid;
                thrdpara[index].remote_ip = confInfo->remote_ip;
                thrdpara[index].remote_port = confInfo->remote_port;
                thrdpara[index].remote_socket_type = confInfo->remote_socket_type;
            } else {
                thrdpara[index].pktid = 0xFFFFFFFF;
                thrdpara[index].remote_ip = confInfo->dremote_ip;
                thrdpara[index].remote_port = confInfo->dremote_port;
                thrdpara[index].remote_socket_type = confInfo->dremote_socket_type;
            }
            bzero(thrdpara[index].exitFlag, 4);
            
            thrdpara[index].sgg_mode = confInfo->sgg_mode;
            thrdpara[index].flag = index;
            thrdpara[index].usedflag = 1;
            thrdpara[index].stopflag = 0;
            thrdpara[index].client_closewaiting = 0;
            retValue = createTcpRecvQueue(thrdpara[index].queue[dir], TCP_RECV_QUEUE_MAX_COUNT, confInfo->tcp_buff_max_count);
            
            ELOG(L_INFO,"pktdir=%d, sd=%d",dir, accept_st);
            threadpool_add_task(thrdPools, TcpProxWriteTask, &thrdpara[index], &thrdpara[index].pktdir[2*dir], NULL);
            threadpool_add_task(thrdPools, TcpProxReadTask, &thrdpara[index], &thrdpara[index].pktdir[2*dir+1], NULL);
            //index = ((++index)%(TCP_PROX_SEND_TASK_MAX_NUM+1));  
            
        }while(!g_stopflag);

        throw(0);
        
    } 
    catch(...) 
    {
        (void)threadpool_destroy(thrdPools);
        close(socket_id);

        if (thrdpara != NULL) {
            for (i=0; i<confInfo->sgg_multi_lines;i++) {
                destroyTcpRecvQueue(thrdpara[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
                destroyTcpRecvQueue(thrdpara[i].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT);
            }
            free(thrdpara);
            thrdpara = NULL;
        }
        return NULL;
    }
}

int runForwardProxyServer(void*arg) {
    static pthread_t upthrd = 0;
    static pthread_t downthrd = 0;
    int udir = PACKET_DIRECTION_UP;
    int ddir = PACKET_DIRECTION_DOWN;
    if (pthread_create(&upthrd, NULL, run_tcpproxserver, &udir) != 0) {
        goto end;
    }
    DEBUG_PRINT("[%s][%d]:[%s] create the tcp proxy server of up direction successful\r\n",__FILE__, __LINE__,__FUNCTION__)
    ELOG(L_INFO,"create the tcp proxy server of up direction successful.");
    pthread_detach(upthrd);

    if (pthread_create(&downthrd, NULL, run_tcpproxserver, &ddir) != 0) {
        goto end;
    }
    DEBUG_PRINT("[%s][%d]:[%s] create the tcp proxy server of down direction successful\r\n",__FILE__, __LINE__,__FUNCTION__)
    ELOG(L_INFO,"create the tcp proxy server of down direction successful");
    pthread_join(downthrd, NULL);
	
    end:
    if (upthrd != 0) {
        pthread_cancel(upthrd);
    }
    if (downthrd != 0) {
        pthread_cancel(downthrd);
    }
    ELOG(L_INFO,"forward proxy server exit...");
    DEBUG_PRINT("forward proxy server exit...");
    return -1;

}

int runReverseProxyServer(void*arg) {
    threadpool_t *thrdPools = NULL;
     SggConfInfo *confInfo = getSggConfInfo();
    int retValue = 0;
    int socket_id=-1, accept_st=-1;
    tcpprox_thread_para *thrdpara = NULL;
    static unsigned int g_pktid = 0;
    int index = -1;
    unsigned int i;
    try 
    {
        thrdpara = (tcpprox_thread_para*)malloc(sizeof(tcpprox_thread_para)*(confInfo->sgg_multi_lines));
        if (thrdpara == NULL) {
            ELOG(L_ERROR,"the tcp socket id of send is invalid");
            DEBUG_PRINT("the tcp socket id of send is invalid\r\n");
            return -1;
        }
        memset(thrdpara, 0, sizeof(tcpprox_thread_para)*(confInfo->sgg_multi_lines));
        for (i=0; i<confInfo->sgg_multi_lines;i++) {
            pthread_cond_init(&(thrdpara[i].queue[PACKET_DIRECTION_UP].cond), NULL);
            pthread_cond_init(&(thrdpara[i].queue[PACKET_DIRECTION_DOWN].cond), NULL);
            pthread_mutex_init(&(thrdpara[i].queue[PACKET_DIRECTION_UP].lock), NULL);
            pthread_mutex_init(&(thrdpara[i].queue[PACKET_DIRECTION_DOWN].lock), NULL); 
            pthread_mutex_init(&(thrdpara[i].lock), NULL);
        }

        socket_id = create_tcplisten(confInfo->listen_ip, confInfo->listen_port, TCP_PROX_PROTOCAL_BUFF_MAX_SIZE); //创建监听socket
        if (socket_id < 0) {
            retValue =  -1;
            throw retValue;
        }
    
        thrdPools = threadpool_create(confInfo->sgg_multi_lines*4, confInfo->sgg_multi_lines*8, confInfo->sgg_multi_lines*8);
        if (thrdPools == NULL) {
            ELOG(L_ERROR,"create thread pools failed");
            DEBUG_PRINT("create thread pools failed\r\n");
            retValue = -1;
            throw retValue;
        }
        
        do {
            accept_st = accept_socket(socket_id,true); //获取连接的的socket
            do {
                index = ((index+1)%(confInfo->sgg_multi_lines));
                BREAK_IF(thrdpara[index].usedflag == 0);
                
            }while(1);
            thrdpara[index].pktdir[0] = PACKET_DIRECTION_UP;
            thrdpara[index].pktdir[1] = PACKET_DIRECTION_UP;
            thrdpara[index].pktdir[2] = PACKET_DIRECTION_DOWN;
            thrdpara[index].pktdir[3] = PACKET_DIRECTION_DOWN;
            thrdpara[index].pktid = ++g_pktid;
            thrdpara[index].client_sid = -2;
            thrdpara[index].client_sid2 = -2;
            thrdpara[index].server_sid = accept_st;
            thrdpara[index].location = confInfo->location;
            thrdpara[index].remote_ip = confInfo->remote_ip;
            thrdpara[index].remote_port = confInfo->remote_port;
            thrdpara[index].remote_socket_type = confInfo->remote_socket_type;      
            thrdpara[index].dremote_ip = confInfo->dremote_ip;
            thrdpara[index].dremote_port = confInfo->dremote_port;
            thrdpara[index].dremote_socket_type = confInfo->dremote_socket_type;
            
            thrdpara[index].sgg_mode = confInfo->sgg_mode;
            thrdpara[index].flag = index;
            thrdpara[index].usedflag = 1;
            thrdpara[index].stopflag = 0;
            thrdpara[index].client_closewaiting = 0;
            bzero(thrdpara[i].exitFlag, 4);
            retValue = createTcpRecvQueue(thrdpara[index].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT, confInfo->tcp_buff_max_count);
            if (retValue < 0) {
                thrdpara[index].usedflag = 0;
                ELOG(L_ERROR,"createTcpRecvQueue failed:%d", retValue);
                DEBUG_PRINT("createTcpRecvQueue failed:%d\r\n", retValue);
                continue;
            }
            retValue = createTcpRecvQueue(thrdpara[index].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT, confInfo->tcp_buff_max_count);
            if (retValue < 0) {
                thrdpara[index].usedflag = 0;
                destroyTcpRecvQueue(thrdpara[index].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
                ELOG(L_ERROR,"createTcpRecvQueue failed:%d", retValue);
                DEBUG_PRINT("createTcpRecvQueue failed:%d\r\n", retValue);
                continue;
            }
            
            threadpool_add_task(thrdPools, TcpProxWriteTask, &thrdpara[index], &thrdpara[index].pktdir[0], NULL);
            threadpool_add_task(thrdPools, TcpProxReadTask, &thrdpara[index], &thrdpara[index].pktdir[1], NULL);
            threadpool_add_task(thrdPools, TcpProxWriteTask, &thrdpara[index], &thrdpara[index].pktdir[2], NULL);
            threadpool_add_task(thrdPools, TcpProxReadTask, &thrdpara[index], &thrdpara[index].pktdir[3], NULL);
            //index = ((++index)%(TCP_PROX_SEND_TASK_MAX_NUM+1));  
            
        }while(!g_stopflag);

        throw(0);
        
    } 
    catch(...) 
    {
        (void)threadpool_destroy(thrdPools);
        close(socket_id);

        if (thrdpara != NULL) {
            for (i=0; i<confInfo->sgg_multi_lines;i++) {
                destroyTcpRecvQueue(thrdpara[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
                destroyTcpRecvQueue(thrdpara[i].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT);
            }
            free(thrdpara);
            thrdpara = NULL;
        }
        return -1;
    }
}

