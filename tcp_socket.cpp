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
#include <signal.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <map>
#include "utils.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "log.h"
#include "threadpool.h"
#include "sgg_err.h"
#include "sgg_manage.h"
#include "sgg_stack.h"


unsigned long long g_tcpDataStat[65535] = {0};


extern int g_stopflag;


void sighander_tcpsocket(int signo)
{
    //printf("********sighander_tcpsocket:%d***********\r\n", signo);
    signal(signo, sighander_tcpsocket);
    return;
}

void sigdismiss_tcpsocket(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    sigprocmask(SIG_BLOCK, &set, NULL);
}
int run_tcpclient(int ipaddr, int port, int sendInterval, bool blockflag)
{
    int client_sd;
    int con_rv;
    int sndBufMaxSize = 0;
    int opt = TCP_PROTOCAL_WBUFF_MAX_SIZE;
    socklen_t optlen = sizeof(sndBufMaxSize);

    struct sockaddr_in client_sockaddr; //定义IP地址结构
    socklen_t addrlen = sizeof(client_sockaddr);
    client_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sd == -1) {
        DEBUG_PRINT("create tcp send client error:%s \n", strerror(errno));
        return -ERR_TCP_SOCKET_CLIENT_CREATE_FAILED;
    }

    memset(&client_sockaddr, 0, sizeof(client_sockaddr));
    client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
    client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
    client_sockaddr.sin_addr.s_addr = htonl(ipaddr); //将字符串的ip地址转换成int型,客服端要连接的ip地址
    con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr, sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
    if (con_rv == -1) {
        close_tcpclient(client_sd);
        //DEBUG_PRINT("TCP socket connect error:%s \n", strerror(errno));
        ELOG(L_ERROR,"TCP socket connect error: sid=%d, ipaddr=0x%08X, port=%u, err=%s", client_sd, ipaddr, port, strerror(errno));
        return -ERR_TCP_SOCKET_CLIENT_CONNECT_FAILED;
    }

    if (blockflag == true) {
        struct timeval timeout = {0,50};  
        if(setsockopt(client_sd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
        {
            DEBUG_PRINT("[run_tcpclient] setsockopt(SO_RCVTIMEO) error:%s \n", strerror(errno));
    		return -ERR_TCP_SOCKET_SET_OPT_FAILED;
        }
        if(setsockopt(client_sd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
        {
            DEBUG_PRINT("[run_tcpclient] setsockopt(SO_SNDTIMEO) error:%s \n", strerror(errno));
    		return -ERR_TCP_SOCKET_SET_OPT_FAILED;
        }

    }    

    if (getsockopt(client_sd, SOL_SOCKET, SO_SNDBUF, &sndBufMaxSize, &optlen) < 0) {
        DEBUG_PRINT("[run_tcpclient] getsockopt error=%d(%s)!!!\n", errno, strerror(errno));
        return -1;
    }
    DEBUG_PRINT("[%s][%d]: [run_tcpclient] OS default tcp socket snd buff size is %d,%d\n", __FILE__, __LINE__,sndBufMaxSize, optlen);

    optlen = sizeof(sndBufMaxSize);
    if ((opt>sndBufMaxSize) && (setsockopt(client_sd, SOL_SOCKET, SO_SNDBUF, &opt, optlen) < 0)) {
        DEBUG_PRINT("[run_tcpclient] setsockopt error=%d(%s)!!!\n", errno, strerror(errno));
        return -1;
    }    
    
    getsockname(client_sd, (struct sockaddr*) &client_sockaddr, &addrlen);

    /*int keepalive = 1; // 开启keepalive属性
    int keepidle = 10; // 如该连接在10秒内没有任何数据往来,则进行探测
    int keepinterval = 5; // 探测时发包的时间间隔为5 秒
    int keepcount = 3; // 探测尝试的次数.如果第1次探测包就收到响应了,则后2次的不再发.
    if (setsockopt(client_sd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive )) < 0) {
        DEBUG_PRINT("TCP set socket option failed: sid=%d\r\n", client_sd, SO_KEEPALIVE);
    }
    if (setsockopt(client_sd, SOL_TCP, TCP_KEEPIDLE, (void*)&keepidle , sizeof(keepidle )) < 0) {
        DEBUG_PRINT("TCP set socket option failed: sid=%d\r\n", client_sd, TCP_KEEPIDLE);
    }
    if (setsockopt(client_sd, SOL_TCP, TCP_KEEPINTVL, (void *)&keepinterval , sizeof(keepinterval )) < 0) {
        DEBUG_PRINT("TCP set socket option failed: sid=%d\r\n", client_sd, TCP_KEEPINTVL);
    }
    if (setsockopt(client_sd, SOL_TCP, TCP_KEEPCNT, (void *)&keepcount , sizeof(keepcount )) < 0) {
        DEBUG_PRINT("TCP set socket option failed: sid=%d\r\n", client_sd, TCP_KEEPCNT);
    }*/
    /* 接管信号量SIGPIPE */
    //signal(SIGPIPE, sighander_tcpsocket);
    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();
    DEBUG_PRINT("create tcp send client succesful: sid=%d\r\n", client_sd);
    ELOG(L_INFO,"create tcp send client succesful: sid=%d, src_port=%d", client_sd, ntohs(client_sockaddr.sin_port));
    if(client_sd>=0)
        g_tcpDataStat[client_sd] = 0;
	return client_sd;
}

void close_tcpclient(int sid)
{    
    if (sid >= 0) {
        close(sid);
        ELOG(L_INFO,"close tcp send client succesful: sid=%d, pkt_send=%llu", sid, g_tcpDataStat[sid]);
    }    
    
    //DEBUG_PRINT("close tcp send client succesful:sid=%d\r\n", sid);    
	return;
}

int send_tcpdata(int sid, char*buf, int dataLen)
{
    int sendDataNum = -1;
    int need_send = dataLen;
    int times = 500;
    char *pbuf = buf;
    
 	if (sid < 0 || buf == NULL || dataLen <= 0) {
		//DEBUG_PRINT("send_tcpdata: para is invalid:sid=%d,dataLen=%d\r\n",sid,dataLen);
		return -ERR_TCP_SOCKET_PARA_INVALID;
	}

    do {
        sendDataNum = send(sid, pbuf, need_send, 0);
        if (sendDataNum == -1) {
            DEBUG_PRINT("send_tcpdata: tcp send error:%s \n", strerror(errno));
            //LOG_OF_DAY("[%s][%d]:[%s] tcp send error:sid=%d, sendDataNum=%d, need_send=%d, times=%d, err=%d, %s \n", __FILE__, __LINE__, __FUNCTION__,sid, sendDataNum, need_send, times, errno, strerror(errno)); 
            EXEC_IF(errno==EAGAIN, usleep(100));
            BREAK_IF((errno==EPIPE)||(errno==EBADF));
        } else {
            need_send -= sendDataNum;
            pbuf += sendDataNum;
        }        
        /* 防止死循环一直退不出，循环100次退出 */
        times--;
        if (need_send >0) {
            usleep(10000);
        }
    }while((need_send > 0) && (times>0));

    g_tcpDataStat[sid]+=(dataLen-need_send);

    /* 此处存在瑕疵，即100次尝试都没发送完成，则整条报文重新发送，此种场景理论上存在，后续待优化 */
    if (need_send > 0) {
        ELOG(L_WARNING,"tcp send error:sid=%d, dataLen=%d, need_send=%d, sendDataNum=%d, times=%d, err=%d, %s", sid, dataLen, need_send, sendDataNum, times, errno, strerror(errno)); 
        return -ERR_TCP_SOCKET_CLIENT_SEND_FAILED;
    } else {
        DEBUG_PRINT("send_tcpdata: tcp send data success:%d,%s \n", sendDataNum,buf);
        return 0;
    } 
}

int addTcpEndData2Queue(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int flag) {
    SendPackStr *sendpkt = (SendPackStr*)pbuf;
    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    if (isfinished == 1) {
        //usleep(50);
        return 0;
    }
    sendpkt->head.offset = offset++;
    sendpkt->head.pktTotalLen = 0;
    sendpkt->head.buf_size = 0;
    sendpkt->head.pktFlag = pktid;
    //if(remote_socket_type == NET_TYPE_UDP && location == NET_LOCATION_CLIENT) {
        sendpkt->head.magicWord = MAGIC_WORD_BANK;
    //} 
    isfinished = 1;
    return -1;
}


int recvTcpUpData(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int flag) {
    int rv = 0;
    int retValue = 0;
    SendPackStr *sendpkt = (SendPackStr*)pbuf;

    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    if (isfinished == 1) {
        //usleep(50);
        return retValue;
    }
    
    rv = recv(sd, sendpkt->buf, buf_len-sizeof(UserHeadInfo), 0);
    if (rv <= 0) {
        /* 非阻塞tcp    socket正常连接场景 */
        if((rv < 0 && (errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN))) {
            /* 需要回退申请的pbuf */
            retValue = 0;
        } else { /*非阻塞式tcp socket退出场景后处理*/
            DEBUG_PRINT("[recvTcpUpData] recv ended[pktdir=%d]:%s \n", pktdir, strerror(errno));
            /*LOG_OF_DAY("[%s][%d]: recvTcpUpData recv ended... :flag=%d, pktdir=%u, rv=%d, errno=%d, server_sid=%d, socket_type=%d, location=%d, buf_len=%d\r\n", __FILE__, __LINE__, 
                flag, pktdir, rv, errno, sd, remote_socket_type, location, buf_len);*/
            /* 发送结束尾包 */
            sendpkt->head.offset = offset++;
            sendpkt->head.pktTotalLen = 0;
            sendpkt->head.buf_size = 0;
            sendpkt->head.pktFlag = pktid;
            //if(remote_socket_type == NET_TYPE_UDP && location == NET_LOCATION_CLIENT) {
                sendpkt->head.magicWord = MAGIC_WORD_BANK;
            //} 
            isfinished = 1;
            retValue = -1;
        }
    } else {        
        sendpkt->head.magicWord = MAGIC_WORD_MAIN;
        sendpkt->head.pktFlag = pktid;
        sendpkt->head.offset = offset++;
        sendpkt->head.buf_size = rv;
        sendpkt->head.pktTotalLen = rv;            
        DEBUG_PRINT("size=%d:%s\r\n", rv, sendpkt->buf); //输出接受到内容
        retValue = 1;
        /*LOG_OF_DAY("[%s][%d]: recvTcpUpData recv data... :flag=%d, pktdir=%u, rv=%d, errno=%d, server_sid=%d, socket_type=%d, location=%d, buf_len=%d\r\n", __FILE__, __LINE__, 
                flag, pktdir, rv, errno, sd, remote_socket_type, location, buf_len);*/
        //CONTINUE_IF(remote_socket_type >= NET_TYPE_BUTT);
    }        
    return retValue;
}

int recvTcpDownData(int sd, char*pbuf, int buf_len, unsigned int&offset, unsigned int&pktid, int&isfinished,
                        unsigned int pktdir, int remote_socket_type, int location, int flag){
    int rv = 0;
    SendPackStr *sendpkt = (SendPackStr*)pbuf;
    int retValue = 0;

    /* 已经收到接收标志，表示正在等待退出，需要回退申请的pbuf */
    RET_IF((isfinished == 1), retValue);
    
    if (sd == -1) {
        /* 需要回退申请的pbuf */
        //usleep(50);        
        retValue = 0;        
    } else if (isfinished == 0) {
        //LOG_OF_DAY("recvTcpDownData_0: flag=%d, pktdir=%d, sd=%d, pktid=%d,isfinished=%d, remote_socket_type=%d, location=%d, rv=%d, error=%d\r\n",flag, pktdir, sd, pktid,isfinished, remote_socket_type, location,rv, errno);
        rv = recv(sd, sendpkt->buf, buf_len-sizeof(UserHeadInfo), 0);
        //LOG_OF_DAY("recvTcpDownData_5: flag=%d, pktdir=%d, sd=%d, pktid=%d,isfinished=%d, remote_socket_type=%d, location=%d, rv=%d, error=%d\r\n",flag, pktdir, sd, pktid,isfinished, remote_socket_type, location,rv, errno);
        if (rv > 0) {
            sendpkt->head.magicWord = MAGIC_WORD_MAIN;
            sendpkt->head.pktFlag = pktid;
            sendpkt->head.offset = offset++;
            sendpkt->head.buf_size = rv;
            sendpkt->head.pktTotalLen = rv;            
            DEBUG_PRINT("size=%d:%s\r\n", rv, sendpkt->buf); //输出接受到内容
            //LOG_OF_DAY("recvTcpDownData_2:pktid=%d, isfinished=%d, pktdir=%d, remote_socket_type=%d, location=%d\r\n",pktid, isfinished, pktdir, remote_socket_type, location);
            retValue = 1;
        } else if(rv <= 0) {
            if (rv == 0 || (errno!=EINTR && errno!=EWOULDBLOCK && errno!=EAGAIN)){
                /* 表示提前检测到socket结束，将isfinished标志置为2为中间状态，后续不再跑recv，等待上一流程来结束，最后isfinished为1时为真正的结束 */            
                isfinished = 2;
                //LOG_OF_DAY("recvTcpDownData_3:pktid=%d, isfinished=%d, pktdir=%d, remote_socket_type=%d, location=%d\r\n",pktid, isfinished, pktdir, remote_socket_type, location);
            } 
            retValue = 0;
        }
    }else if(isfinished == 2){
        sleep(1);
        retValue = 0; 
    }
       
    return retValue;
}

void *addTcpWriteTask(void *arg1, void *arg2, void*arg3)
{
    tcp_thread_para* para = (tcp_thread_para*)arg1;
    int location = para->location;
    int remote_socket_type = para->remote_socket_type;
    int remote_port = para->remote_port;
    unsigned int remote_ip = para->remote_ip;
    int thread_flag = para->flag;
    int wpos;
    int sd = -1;
    char *pbuf = NULL;
    unsigned int offset = 0;    
    unsigned int pktid = para->pktid;
    
    int isfinished = 0;
    int retValue = 0;
    unsigned int pktdir = *((unsigned int*)arg2);
    DEBUG_PRINT("addTcpWriteTask ining... :flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d, socket_type=%d, location=%d,pktid=%u.\r\n", 
        thread_flag,pktdir,para->server_sid, para->client_sid, remote_ip, remote_port, remote_socket_type, location, pktid);
    ELOG(L_INFO,"addTcpWriteTask ining... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d, socket_type=%d, location=%d,pktid=%u.", 
        thread_flag, pktdir, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location, pktid);
    if (pktdir >= PACKET_DIRECTION_BUTT) {
        goto end;
    }

    /* 接管信号量SIGPIPE */
    //signal(SIGPIPE, sighander_tcpsocket);
    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();
    
    while(!para->shutdown) {
        wpos = para->queue[pktdir].wpos; 
        if(PushStack(para->queue[pktdir].PktBufStack[wpos], &pbuf)==-1) {
            if (wpos == para->queue[pktdir].rpos) {
                pthread_mutex_lock(&(para->queue[pktdir].lock));
                pthread_cond_signal(&(para->queue[pktdir].cond));
                para->queue[pktdir].wpos = ((++wpos)%TCP_RECV_QUEUE_MAX_COUNT);
                pthread_mutex_unlock(&(para->queue[pktdir].lock));
                BREAK_IF(isfinished == 1);
            } else {
                usleep(50);
            }
            /*LOG_OF_DAY("[%s][%d]: addTcpWriteTask full... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, old_sd=%d, socket_type=%d, location=%d, isfinished=%d, wpos=%d.\r\n", __FILE__, __LINE__, 
                        thread_flag, pktdir, para->server_sid, para->client_sid, sd, remote_socket_type, location, isfinished, wpos);*/
            continue;
        }
        
        if (pktdir == PACKET_DIRECTION_UP) {
            if (para->stop_flag == 1) {
                retValue = addTcpEndData2Queue(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                    pktdir, remote_socket_type, location, thread_flag);
            } else {
                retValue = recvTcpUpData(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                    pktdir, remote_socket_type, location, thread_flag);
            }
        } else {
            if (sd != para->client_sid) {
                if (sd > 0 && para->client_sid >0 && isfinished==2) {
                    isfinished = 0;
                    ELOG(L_INFO,"addTcpWriteTask update... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, old_sd=%d, socket_type=%d, location=%d, isfinished=%d.",  
                        thread_flag, pktdir, para->server_sid, para->client_sid, sd, remote_socket_type, location, isfinished);
                }
                sd = para->client_sid;
            }

            if (para->client_closewaite != 1) {
                retValue = recvTcpDownData(para->client_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                pktdir, remote_socket_type, location, thread_flag); 
            } else {
                retValue = addTcpEndData2Queue(para->server_sid, pbuf, PER_PACKET_MAX_SIZE, offset, pktid, isfinished, 
                                    pktdir, remote_socket_type, location, thread_flag);
            }
            
            if (isfinished == 2) {
                para->stop_flag = 1;
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
        /*if(para->client_sid != -1){
            LOG_OF_DAY("[%s][%d]: addTcpWriteTask cycle... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, socket_type=%d, location=%d, isfinished=%d.\r\n", __FILE__, __LINE__, 
            thread_flag, pktdir, para->server_sid, para->client_sid, remote_socket_type, location, isfinished);
        }*/
        
    } 
    /* 外部强制退出，通知读线程退出 */
    if(para->shutdown==1) {
        pthread_mutex_lock(&(para->queue[pktdir].lock));
        pthread_cond_broadcast(&(para->queue[pktdir].cond));
        pthread_mutex_unlock(&(para->queue[pktdir].lock));
    }
end:
    if(pktdir == PACKET_DIRECTION_UP){
        if (remote_socket_type != NET_TYPE_TCP) {
            close(para->server_sid);
            para->server_sid = -1;
        } 
    } else if (pktdir == PACKET_DIRECTION_DOWN) {
        close_tcpclient(para->client_sid);
        para->client_sid  = -1;
    }


    if(pktdir < PACKET_DIRECTION_BUTT) {
        pthread_mutex_lock(&(para->lock));
        para->exitFlag[pktdir*PACKET_DIRECTION_BUTT] = 1;
        pthread_mutex_unlock(&(para->lock));
    }
    
    DEBUG_PRINT("addTcpWriteTask outing... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n", 
        thread_flag,pktdir,para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    ELOG(L_INFO,"addTcpWriteTask outing... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.", 
        thread_flag,pktdir, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    return NULL;

}

void *addTcpReadTask(void *arg1, void *arg2, void*arg3) {
    tcp_thread_para* para = (tcp_thread_para*)arg1;
    int location = para->location;
    int remote_socket_type = para->remote_socket_type;
    unsigned int remote_ip = para->remote_ip;
    unsigned int remote_port = para->remote_port;
    unsigned int udp_send_interval = para->udp_send_interval;
    int thread_flag = para->flag;
       
    int retValue = 0;
    unsigned int pktdir = *((unsigned int*)arg2);
    int rpos = -1;
    int send_sid = -1;
    char *pbuf;
    SendPackStr *sendpkt = NULL;
    int count = 0;
    int failedtimes = 0;
    int heartCounter = 0;
    
    DEBUG_PRINT("addTcpReadTask ining...: flag=%d, pktdir=%u,remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n",
        thread_flag, pktdir, remote_ip, remote_port, remote_socket_type, location);
    ELOG(L_INFO,"addTcpReadTask ining...: flag=%d, pktdir=%u, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.",  
        thread_flag, pktdir,remote_ip, remote_port,remote_socket_type, location);

    if (pktdir >= PACKET_DIRECTION_BUTT) {
        goto end;
    }

    /* 接管信号量SIGPIPE */
    //signal(SIGPIPE, sighander_tcpsocket);
    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();

    if (pktdir == PACKET_DIRECTION_UP) {
        retValue = checkTcpClientSocketAlive(remote_socket_type, thread_flag, pktdir, remote_ip, remote_port, para->client_sid);
        send_sid = para->client_sid;
    } else {
        send_sid = para->server_sid;
    }


    while (!para->shutdown) {
        rpos = para->queue[pktdir].rpos;
        if (IsStackEmpty(para->queue[pktdir].PktBufStack[rpos])) {
            pthread_mutex_lock(&(para->queue[pktdir].lock));
            para->queue[pktdir].rpos = para->queue[pktdir].wpos;
            pthread_cond_wait(&(para->queue[pktdir].cond), &(para->queue[pktdir].lock));
            rpos = para->queue[pktdir].rpos;
            pthread_mutex_unlock(&(para->queue[pktdir].lock));            
        }
        retValue = PopStack(para->queue[pktdir].PktBufStack[rpos], &pbuf); 
        if (retValue!=-1) {
            sendpkt = (SendPackStr*)pbuf;
            BREAK_IF(sendpkt->head.magicWord == MAGIC_WORD_BANK);
            CONTINUE_IF(sendpkt->head.buf_size == 0);
            
            if (pktdir == PACKET_DIRECTION_UP) {
                checkTcpClientSocketAlive(remote_socket_type, thread_flag, pktdir, remote_ip, remote_port, para->client_sid, para->shutdown);
                send_sid = para->client_sid;
            }
            
            //printf("addTcpReadTask_2:socket_type=%d, send_sid=%d,location=%d\r\n", remote_socket_type, send_sid,location);
            if(remote_socket_type == NET_TYPE_UDP) {            
                if(location == NET_LOCATION_CLIENT) {
                    /* 如下为模拟丢包测试代码 */
                    //if(offset++ %10==0)  continue;
                    //sendpkt->head.offset = offset++;
                    //sendpkt->head.pktTotalLen = sendpkt->head.buf_size;
                    retValue = send_udpdata(send_sid, pbuf, sendpkt->head.buf_size+sizeof(UserHeadInfo), udp_send_interval, true);
                    if(retValue < 0) {
                        if (retValue == -ERR_UDP_SOCKET_ACK_STACK_FULL_WITH_HEART) {
                            BackPopStack(para->queue[pktdir].PktBufStack[rpos]);
                        } else {
                            /* 对端长时间无响应 */
                            close_udpclient(para->client_sid);
                            para->client_sid = -1;
                            ELOG(L_WARNING,"tcp_send timeout... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, socket_type=%d, send_sid=%d, retValue=%d,pktFlag=%u, offset=%u.",
                                thread_flag, pktdir, para->server_sid, para->client_sid, remote_socket_type, send_sid, retValue, sendpkt->head.pktFlag, sendpkt->head.offset);
                        }
                    }
                    
                } else {
                    retValue = send_udpdata(send_sid, sendpkt->buf, sendpkt->head.buf_size, udp_send_interval, false);
                }
            } else if (remote_socket_type == NET_TYPE_TCP) {
                retValue = send_tcpdata(send_sid, sendpkt->buf, sendpkt->head.buf_size);
                /* 此处进行保护，即尝试调用send接口发送3次，仍然发送失败，则不进行回退，该报文直接丢弃 */
                if ((retValue < 0) && ((++failedtimes) <= 3)) {
                    BackPopStack(para->queue[pktdir].PktBufStack[rpos]);
                } else {
                    failedtimes = 0;
                }
            }
            /*if(retValue < 0) {
                BackPopStack(para->queue[pktdir].PktBufStack[rpos]);
            }*/
                                   
        }
        /*LOG_OF_DAY("[%s][%d]: addTcpReadTask cycle... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, socket_type=%d, location=%d,retValue=%d.\r\n", __FILE__, __LINE__, 
        thread_flag, pktdir, para->server_sid, para->client_sid, remote_socket_type, location, retValue);*/
    }

    if(remote_socket_type == NET_TYPE_UDP && location == NET_LOCATION_CLIENT) {
        /* UDP回包缓存队列中的数据如果超过15s还没有确认完毕，直接发送结束报文并结束本线程 */
        count=0;
        bool ret;
        do {
            heartCounter = GetUdpPktHeartCounter(para->client_sid);
            if(heartCounter==0) {
                count++;
            } else {
                count = 0;
                ResetUdpPktHeartCounter(para->client_sid);
            }
            ret = checkAllUdpPktsAcked(para->client_sid);
            usleep(10000);
        } while(count<1500 && (ret==false));
        if(ret==false){
            ELOG(L_WARNING,"any pkts unacked... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, socket_type=%d, send_sid=%d, retValue=%d,pktFlag=%u, offset=%u.", 
                thread_flag, pktdir, para->server_sid, para->client_sid, remote_socket_type, send_sid, retValue, sendpkt->head.pktFlag, sendpkt->head.offset);
        }
        sendpkt->head.pktTotalLen = 0;
        sendpkt->head.buf_size = 0;
        sendpkt->head.magicWord = MAGIC_WORD_BANK;
        retValue = send_udpdata(send_sid, pbuf, sizeof(UserHeadInfo), udp_send_interval, false);
        ELOG(L_INFO,"send last end pkt... : flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, socket_type=%d, send_sid=%d, retValue=%d,pktFlag=%u.",
            thread_flag, pktdir, para->server_sid, para->client_sid, remote_socket_type, send_sid, retValue, sendpkt->head.pktFlag);
    }
end:    
    if (pktdir == PACKET_DIRECTION_UP) { 
        if (remote_socket_type != NET_TYPE_TCP) {
            close_udpclient(para->client_sid);
            para->client_sid = -1;
        }
        para->client_closewaite = 1;
    } else {
        close(para->server_sid);
        para->server_sid = -1;
        
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
                ELOG(L_WARNING,"timeout...: flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d, shutdown=%d, rpos=%d.", 
                    thread_flag,pktdir, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location, para->shutdown, rpos);
                count = 0;
            }
            usleep(1000);
        }while(1);
    }

    /*if(pktdir<PACKET_DIRECTION_BUTT) {
        destroyTcpRecvQueue(para->queue[pktdir], TCP_RECV_QUEUE_MAX_COUNT);
    }*/
    EXEC_IF((pktdir == PACKET_DIRECTION_DOWN || ((pktdir == PACKET_DIRECTION_UP) && (remote_socket_type != NET_TYPE_TCP))), (para->usedflag = 0));


    DEBUG_PRINT("addTcpReadTask outing...: flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d.\r\n", 
        thread_flag,pktdir, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location);
    ELOG(L_INFO,"addTcpReadTask outing...: flag=%d, pktdir=%u, server_sid=%d, client_sid=%d, remote_ip=0x%08X, remote_port=%d,socket_type=%d, location=%d, rpos=%d.",  
        thread_flag,pktdir, para->server_sid, para->client_sid, remote_ip, remote_port,remote_socket_type, location, rpos);
    return NULL;

}

int create_tcplisten(int ipaddr, int port, int rcvBufMaxSize, int blockFlag)
{ 
	int socket_id;
	struct sockaddr_in sockaddr; //定义IP地址结构
	int on = 1;
	socket_id = socket(AF_INET, SOCK_STREAM, 0); //初始化socket
	if (socket_id == -1) {
		ELOG(L_ERROR,"create tcp server socket failed:ipaddr=0x%08X, port=%d, err=%s", ipaddr, port, strerror(errno));
		return -1;
	}

    // 查看系统默认的socket接收缓冲区大小
    int defRcvBufSize = -1;
    socklen_t optlen = sizeof(defRcvBufSize);
    if (getsockopt(socket_id, SOL_SOCKET, SO_RCVBUF, &defRcvBufSize, &optlen) < 0) {
        ELOG(L_ERROR,"[create_tcplisten(ip=0x%08X, port=%d)] getsockopt error=%d(%s)!!!", ipaddr, port,errno, strerror(errno));
        return -1;
    }
    ELOG(L_INFO,"OS default tcp socket recv buff size is %d, ipaddr=0x%08X, port=%d", defRcvBufSize, ipaddr, port);

    optlen = sizeof(rcvBufMaxSize);
    if ((rcvBufMaxSize>defRcvBufSize) && (setsockopt(socket_id, SOL_SOCKET, SO_RCVBUF, &rcvBufMaxSize, optlen) < 0)) {
        ELOG(L_ERROR,"[create_tcplisten] setsockopt error=%d(%s)!!!", errno, strerror(errno));
        return -1;
    }

	if (setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) //设置ip地址可重用
	{
		ELOG(L_ERROR,"[create_tcplisten] setsockopt error:%s", strerror(errno));
		return -1;
	}

    if (blockFlag==0) {
        int flags = fcntl(socket_id, F_GETFL, 0);
        fcntl(socket_id, F_SETFL, flags | O_NONBLOCK);
    }

    /*if(noBlockFlag>0) {
        struct timeval timeout = {0,10};  
        if(setsockopt(socket_id, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
        {
            DEBUG_PRINT("[create_tcpsocket] setsockopt error:%s \n", strerror(errno));
    		return -ERR_UDP_SOCKET_SET_OPT_FAILED;
        }
    }*/
    
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
		ELOG(L_ERROR,"[create_tcplisten] bind error:%s", strerror(errno));
		return -1;
	}

    if (listen(socket_id, 10000) == -1) { //    服务端开始监听
        ELOG(L_ERROR,"[create_tcplisten] listen error:%s", strerror(errno));
        return -1;
    }
    ELOG(L_INFO,"create tcp listen port succesful: sid=%d,ip=0x%08x, port=%u", socket_id, ipaddr, port);
	return socket_id;
}

int accept_socket(int listen_st, bool blockflag)
{
    int accept_st;
    struct sockaddr_in accept_sockaddr; //定义accept IP地址结构
    socklen_t addrlen = sizeof(accept_sockaddr);
    memset(&accept_sockaddr, 0, addrlen);
    accept_st = accept(listen_st, (struct sockaddr*) &accept_sockaddr,&addrlen);
    //accept 会阻塞直到客户端连接连过来 服务端这个socket只负责listen 
    // 是不是有客服端连接过来了,是通过accept返回socket通信的
    if (accept_st == -1)
    {
        if(errno==EAGAIN || errno==EWOULDBLOCK) {
            return -2;
        } else {
            ELOG(L_WARNING,"[accept_socket]accept error:lsid=%d, %s", listen_st, strerror(errno));
            return -1;
        }
    }

    if (blockflag == true) {
        struct timeval timeout = {0,50};  
        if(setsockopt(accept_st, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
        {
            ELOG(L_ERROR,"[accept_socket] setsockopt(SO_RCVTIMEO) error:%s", strerror(errno));
    		return -ERR_TCP_SOCKET_SET_OPT_FAILED;
        }

        if(setsockopt(accept_st, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
        {
            ELOG(L_ERROR,"[accept_socket] setsockopt(SO_SNDTIMEO) error:%s", strerror(errno));
    		return -ERR_TCP_SOCKET_SET_OPT_FAILED;
        }
    }

    int keepalive = 1; // 开启keepalive属性
    int keepidle = 10; // 如该连接在30秒内没有任何数据往来,则进行探测
    int keepinterval = 5; // 探测时发包的时间间隔为5 秒
    int keepcount = 3; // 探测尝试的次数.如果第1次探测包就收到响应了,则后2次的不再发.
    if (setsockopt(accept_st, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive )) < 0) {
        ELOG(L_ERROR,"TCP set socket option failed: sid=%d", accept_st, SO_KEEPALIVE);
    }
    if (setsockopt(accept_st, SOL_TCP, TCP_KEEPIDLE, (void*)&keepidle , sizeof(keepidle )) < 0) {
        ELOG(L_ERROR,"TCP set socket option failed: sid=%d", accept_st, TCP_KEEPIDLE);
    }
    if (setsockopt(accept_st, SOL_TCP, TCP_KEEPINTVL, (void *)&keepinterval , sizeof(keepinterval )) < 0) {
        ELOG(L_ERROR,"TCP set socket option failed: sid=%d", accept_st, TCP_KEEPINTVL);
    }
    if (setsockopt(accept_st, SOL_TCP, TCP_KEEPCNT, (void *)&keepcount , sizeof(keepcount )) < 0) {
        ELOG(L_ERROR,"TCP set socket option failed: sid=%d", accept_st, TCP_KEEPCNT);
    }
    sigdismiss_tcpsocket();
    DEBUG_PRINT("[accept_socket]accpet ip:sd=%d, ip=%s \n", accept_st, inet_ntoa(accept_sockaddr.sin_addr));
    return accept_st;
}

/*void *checkTCPSocketTask(void *arg1, void *arg2, void*arg3) {
    while (!g_stopflag) {
        check_clientsocket(0xffff);
        sleep(1);
    }
    return NULL;
}*/


tcp_server_obj::tcp_server_obj(ServerCfgInfo &cfg)
{
    memset(&confInfo,0, sizeof(ServerCfgInfo));
    memcpy(&confInfo,&cfg, sizeof(ServerCfgInfo));
    pktid = 0;
    runFlag = 0;
    thrId = 0;
    socket_id = 0;
}

tcp_server_obj::tcp_server_obj(const tcp_server_obj& obj)
{
    pktid = obj.pktid;
    memcpy(&confInfo,&(obj.confInfo), sizeof(ServerCfgInfo));
    DEBUG_PRINT("tcp_server_obj: 0x%08X\n", confInfo.listen_ip);
    runFlag = 0;
    thrId = 0;
    socket_id = 0;
}

tcp_server_obj::~tcp_server_obj()
{
    #if 0
    if(runFlag == 1) {
        (void)threadpool_destroy(thrdPools);
        thrdPools = NULL;
        if(socket_id > 0) {
            close(socket_id);
        }

        if (bufMap != NULL) {
            for (unsigned int i=0; i<confInfo.sgg_multi_lines;i++) {
                destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
                destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT);
            }
            free(bufMap);
            bufMap = NULL;
        }
        
    }
    
    if(thrId != 0) {
        pthread_join(thrId, NULL);
    }
    #endif
}


int tcp_server_obj::init(void)
{
    shutdownFlag = 0;
    runFlag = 0;
    pthread_rwlock_init(&rwlock,NULL);    
    return 0;
}

int tcp_server_obj::getshutdown(void)
{
    int flag;
    pthread_rwlock_rdlock(&rwlock);
    flag = shutdownFlag;
    pthread_rwlock_unlock(&rwlock);
    return flag;
}

void tcp_server_obj::setshutdown(int flag)
{
    pthread_rwlock_wrlock(&rwlock);
    shutdownFlag = flag;
    if(socket_id >=0 ){
        shutdown(socket_id, SHUT_RDWR);
        //socket_id = -1;
    }
    pthread_rwlock_unlock(&rwlock);
    //int sd = run_tcpclient(confInfo.listen_ip, confInfo.listen_port, 0, true);
    //close_tcpclient(sd);
    return;
}


int tcp_server_obj::run(void)
{    
    int retValue = -1;
    unsigned int i;
    int accept_st = -1;
    
    
    ELOG(L_INFO,"starting run tcp server:ipaddr=0x%08X, port=%u, net_type=%u, location=%d, tcp_buff_max_count=%u.", 
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);
    DEBUG_PRINT("starting run tcp server:ipaddr=0x%08X, port=%u, net_type=%u, location=%d,tcp_buff_max_count=%u.\r\n", 
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);

    runFlag = 1;
    bufMap = (tcp_thread_para*)malloc(sizeof(tcp_thread_para)*(confInfo.sgg_multi_lines));
    if (bufMap == NULL) {
        ELOG(L_ERROR,"allocate memory failed:ipaddr=0x%08X, port=%u, net_type=%u, location=%d, tcp_buff_max_count=%u.", 
            confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);
        DEBUG_PRINT("the tcp socket id of send is invalid\r\n");
        return -1;
    }
    memset(bufMap, 0, sizeof(tcp_thread_para)*(confInfo.sgg_multi_lines));

    for (i=0; i<(confInfo.sgg_multi_lines);i++) {
        pthread_cond_init(&(bufMap[i].queue[PACKET_DIRECTION_UP].cond), NULL);
        pthread_cond_init(&(bufMap[i].queue[PACKET_DIRECTION_DOWN].cond), NULL);
        pthread_mutex_init(&(bufMap[i].queue[PACKET_DIRECTION_UP].lock), NULL);
        pthread_mutex_init(&(bufMap[i].queue[PACKET_DIRECTION_DOWN].lock), NULL);
        pthread_mutex_init(&(bufMap[i].lock), NULL);
    }
    
    socket_id = create_tcplisten(confInfo.listen_ip, confInfo.listen_port, TCP_PROTOCAL_BUFF_MAX_SIZE); //创建监听socket
    if (socket_id < 0) {
        ELOG(L_ERROR,"create tcp listen port failed:ipaddr=0x%08X, port=%u, net_type=%u, location=%d, tcp_buff_max_count=%u.", 
            confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);
        retValue =  -1;
        goto end;
    }
    //signal(SIGPIPE, sighander_tcpsocket);
    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();

    i = 0;

    thrdPools = threadpool_create(confInfo.sgg_multi_lines*4, confInfo.sgg_multi_lines*8, confInfo.sgg_multi_lines*8, confInfo.listen_port);
    if (thrdPools == NULL) {
        ELOG(L_ERROR,"create thread pools failed:ipaddr=0x%08X, port=%u, net_type=%u, location=%d, tcp_buff_max_count=%u.", 
            confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);
        DEBUG_PRINT("create thread pools failed\r\n");
        goto end;
    }
    
    i = 0;
    do {
        /* 在创建socket id之前收到删除消息，导致无法正常停止socket id accept操作 */
        if(!getshutdown()) {
            accept_st = accept_socket(socket_id, true); //获取连接的的socket
            if (accept_st == -1) {
                ELOG(L_WARNING,"tcp socket accept failed(ip=0x%08X, port=%u,asid=%d)", confInfo.listen_ip, confInfo.listen_port, accept_st);
                goto end;
            }else if(accept_st == -2) {
                usleep(10);
                continue;
            }            
        } else {
            ELOG(L_INFO,"tcp socket shutdown(ip=0x%08X, port=%u,asid=%d)", confInfo.listen_ip, confInfo.listen_port, accept_st);
            goto end;
        }
        //LOG_OF_DAY("[%s][%d]:[%s] tcp socket accept log(ip=0x%08X, port=%u,asid=%d)\r\n", __FILE__, __LINE__, __FUNCTION__,confInfo.listen_ip, confInfo.listen_port, accept_st);
        pktid += 2;

        do {           
            BREAK_IF(bufMap[i].usedflag == 0);
            i = ((i+1)%(confInfo.sgg_multi_lines));
        }while(1);
        
        bufMap[i].server_sid = accept_st;
        bufMap[i].client_sid = -1;
        bufMap[i].client_closewaite = 0;
        bufMap[i].location = confInfo.location;
        bufMap[i].remote_socket_type = confInfo.remote_socket_type;
        bufMap[i].remote_ip = confInfo.remote_ip;
        bufMap[i].remote_port = confInfo.remote_port;
        bufMap[i].sgg_mode = 0;
        bufMap[i].udp_send_interval = confInfo.udp_send_interval;
        bufMap[i].flag = i;
        bufMap[i].stop_flag = 0;                
        bufMap[i].pktid = pktid;
        bufMap[i].usedflag = 1;
        bzero(bufMap[i].exitFlag, 4);
        bufMap[i].shutdown = 0;
        
        retValue = createTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT, confInfo.tcp_buff_max_count);
        if (retValue < 0) {
            ELOG(L_ERROR,"createTcpRecvQueue failed:%d", retValue);
            DEBUG_PRINT("createTcpRecvQueue failed:%d\r\n", retValue);
            continue;
        }
        if (confInfo.remote_socket_type == NET_TYPE_TCP) {
            retValue = createTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT, confInfo.tcp_buff_max_count);
            if (retValue < 0) {
                destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
                ELOG(L_ERROR,"reateTcpRecvQueue failed:%d", retValue);
                DEBUG_PRINT("createTcpRecvQueue failed:%d\r\n", retValue);
                continue;
            }            
        }
        bufMap[i].pktdir[0] = PACKET_DIRECTION_UP;
        bufMap[i].pktdir[1] = PACKET_DIRECTION_UP;
        threadpool_add_task(thrdPools, addTcpWriteTask, &bufMap[i], &bufMap[i].pktdir[0], NULL);
        threadpool_add_task(thrdPools, addTcpReadTask, &bufMap[i], &bufMap[i].pktdir[1], NULL);
        if (confInfo.remote_socket_type == NET_TYPE_TCP) {
            bufMap[i].pktdir[2] = PACKET_DIRECTION_DOWN;
            bufMap[i].pktdir[3] = PACKET_DIRECTION_DOWN;
            threadpool_add_task(thrdPools, addTcpWriteTask, &bufMap[i], &bufMap[i].pktdir[2], NULL);
            threadpool_add_task(thrdPools, addTcpReadTask, &bufMap[i], &bufMap[i].pktdir[3], NULL);
        }
        
        i = ((i+1)%(confInfo.sgg_multi_lines));        
    }while(!getshutdown());


    if (bufMap != NULL) {
        for (i=0; i<confInfo.sgg_multi_lines;i++) {
            bufMap[i].shutdown = 1;
        }
    }

    (void)threadpool_destroy(thrdPools);
    thrdPools = NULL;
    if (accept_st > -1) {
        close(accept_st);
        accept_st = -1;
    }
    
    if(socket_id >= 0) {
        close(socket_id);
        socket_id = -1;
    }
    for (i=0; i<(confInfo.sgg_multi_lines);i++) {
        destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
        if(confInfo.remote_socket_type == NET_TYPE_TCP) {
            destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT);
        }
        //bufMap[i].shutdown = 1;
    }
    free(bufMap);
    bufMap = NULL;
    //thrId=0;
    runFlag = 2;    
    ELOG(L_INFO,"tcp socket exit:ipaddr=0x%08X, port=%u, net_type=%u, location=%d, tcp_buff_max_count=%u.",
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);
    DEBUG_PRINT("tcp socket exit:%d\r\n", retValue);
    return 0;
end:
    if (accept_st > -1) {
        close(accept_st);
        accept_st = -1;
    }
    
    if(socket_id >= 0) {
        close(socket_id);
        socket_id = -1;
    }
    
    if (bufMap != NULL) {
        for (i=0; i<confInfo.sgg_multi_lines;i++) {
            bufMap[i].shutdown = 1;
        }
    }
    (void)threadpool_destroy(thrdPools);
    thrdPools = NULL;
    if (bufMap != NULL) {
        for (i=0; i<confInfo.sgg_multi_lines;i++) {
            destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_UP], TCP_RECV_QUEUE_MAX_COUNT);
            if(confInfo.remote_socket_type == NET_TYPE_TCP) {
                destroyTcpRecvQueue(bufMap[i].queue[PACKET_DIRECTION_DOWN], TCP_RECV_QUEUE_MAX_COUNT);
            }
            //bufMap[i].shutdown = 1;
        }
        free(bufMap);
        bufMap = NULL;
    }   
    //LOG_OF_DAY("[%s][%d]: tcp socket exit:%d\r\n", __FILE__, __LINE__, retValue);
    ELOG(L_INFO,"tcp socket exit:ipaddr=0x%08X, port=%u, net_type=%u, location=%d, tcp_buff_max_count=%u.", 
        confInfo.listen_ip, confInfo.listen_port, confInfo.listen_socket_type, confInfo.location, confInfo.tcp_buff_max_count);
    DEBUG_PRINT("tcp socket exit:%d\r\n", retValue);
    //thrId=0;
    runFlag = 2;
    return retValue;
        
}

void *tcpSvrManageThrdProc(void *para)
{
    tcp_server_obj *pobj = (tcp_server_obj*)para; 
    //pthread_detach(pthread_self());
    pobj->run();
    pthread_exit(NULL);
}


tcp_server_manage::tcp_server_manage(void*arg)
{
    if(arg != NULL) {
        memcpy(&confInfo, arg, sizeof(SggConfInfo));
    }
    pthread_rwlock_init(&rwlock,NULL);
    servers.clear();
    ELOG(L_INFO,"new obj with para");
}

tcp_server_manage::tcp_server_manage()
{
    pthread_rwlock_init(&rwlock,NULL);
    confInfo.location = NET_LOCATION_BUTT;
    servers.clear();
    ELOG(L_INFO,"new obj without para");
}


tcp_server_manage::~tcp_server_manage()
{
    //std::map<int,tcp_server_obj>::iterator it;
    //tcp_server_obj *obj = NULL;

    /*for (it=servers.begin(); it!=servers.end(); ++it) {
        obj = it->second;        
    }*/

    servers.clear();
    ELOG(L_INFO,"delete obj");
}

int tcp_server_manage::init(void*arg)
{
    if(arg != NULL) {
        memcpy(&confInfo, arg, sizeof(SggConfInfo));
    }    
    return 0;
}

tcp_server_manage* tcp_server_manage::GetInstance()
{
    static tcp_server_manage *instace = NULL;
    if(instace == NULL) {
        instace = new tcp_server_manage;
    }
    return instace;
}

int tcp_server_manage::add(SvrObjCommInfo *commCfg, SvrObjBaseInfo*cfg)
{
    std::map<int,tcp_server_obj>::iterator it;
    std::pair<std::map<int,tcp_server_obj>::iterator,bool> ret;
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
    
    pthread_rwlock_wrlock(&rwlock);    
    
    tcp_server_obj obj(svrCfg);
    ret = servers.insert(std::pair<int,tcp_server_obj>(svrCfg.listen_port, obj));
    
    if (ret.second!=false) {       
        ELOG(L_INFO,"Obj added successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
            cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
    } else {
        ELOG(L_ERROR,"Obj is existed(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
            cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
        pthread_rwlock_unlock(&rwlock);
        return ERR_TCP_SOCKET_SVR_PORT_EXIST;
    }
    it = servers.find(svrCfg.listen_port);
    if(it != servers.end()) {
        tcp_server_obj *pobj = &(it->second);
        if((pobj->init() != 0) || ((retValue =pthread_create(&(pobj->thrId), NULL, tcpSvrManageThrdProc, (void*)pobj))!=0)) {
            servers.erase(it);
            ELOG(L_ERROR,"Init server object failed(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u, ret=%u, %s)",  
                cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol, retValue, strerror(retValue));
        }  else {
            ELOG(L_INFO,"Init server object successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
        }
    }

    pthread_rwlock_unlock(&rwlock);
    
    return 0;
}

int tcp_server_manage::rmv(SvrObjBaseInfo*cfg)
{
    std::map<int,tcp_server_obj>::iterator it;
    RET_IF(confInfo.location>=NET_LOCATION_BUTT, ERR_SGG_SERVER_M_NOT_READY);
    ELOG(L_INFO,"delete tcp server object ining...(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                    cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);
    pthread_rwlock_wrlock(&rwlock);
    it = servers.find(cfg->local.port);
    if(it != servers.end()) {
        tcp_server_obj *pobj = &(it->second);
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
        ELOG(L_INFO,"delete tcp server object successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                    cfg->local.ip, cfg->local.port, cfg->local.protocol, cfg->remote.ip, cfg->remote.port, cfg->remote.protocol);        
    }
    pthread_rwlock_unlock(&rwlock);
    return 0;
}


int tcp_server_manage::rmv(SvrObjBaseInfo*cfg, int num)
{
    std::map<int,tcp_server_obj>::iterator it;
    int i = 0;
    SvrObjBaseInfo *ptr = cfg;    
    RET_IF(confInfo.location>=NET_LOCATION_BUTT, ERR_SGG_SERVER_M_NOT_READY);

    pthread_rwlock_wrlock(&rwlock);
    for(i=0; i<num; i++) {
        it = servers.find(ptr->local.port);
        if(it != servers.end()) {
            tcp_server_obj *pobj = &(it->second);
            pobj->setshutdown(1);
        }
        ptr++;
    }

    ptr = cfg;   
    for(i=0; i<num; ++i) {
        ELOG(L_INFO,"delete tcp server object ining...(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)", 
                    ptr->local.ip, ptr->local.port, ptr->local.protocol, ptr->remote.ip, ptr->remote.port, ptr->remote.protocol);
        it = servers.find(ptr->local.port);
        if(it != servers.end()) {
            tcp_server_obj *pobj = &(it->second);
            if(pobj->thrId != 0) {
                //int kill_rc = pthread_kill(pobj->thrId, 0);     //发送0号信号，测试是否存活
                //if (kill_rc != ESRCH)  //线程存在
                //{
                    //pobj->setshutdown(1);
                    ELOG(L_INFO,"waiting delete tcp server object (listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                        ptr->local.ip, ptr->local.port, ptr->local.protocol, ptr->remote.ip, ptr->remote.port, ptr->remote.protocol);  
                    pthread_join(pobj->thrId, NULL);
                    pobj->thrId = 0;
                    //pthread_cancel(pobj->thrId);
                //}
            }
            servers.erase(it);
            ELOG(L_INFO,"delete tcp server object successful(listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                        ptr->local.ip, ptr->local.port, ptr->local.protocol, ptr->remote.ip, ptr->remote.port, ptr->remote.protocol);  
        }
        ptr++;
    }
    pthread_rwlock_unlock(&rwlock);
    return 0;
}



int tcp_server_manage::run()
{
    ServerCfgInfo svrCfg;
    int ret = 0;
    std::map<int,tcp_server_obj>::iterator it;
    tcp_server_obj *pobj = NULL;

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
    for(unsigned int port=confInfo.listen_port_begin,rport=confInfo.remote_port_begin; ((port<=confInfo.listen_port_end)&&(port>0));port++,rport++) {
        svrCfg.listen_port=port;
        svrCfg.remote_port=rport;
        tcp_server_obj obj(svrCfg);
        servers.insert(std::pair<int,tcp_server_obj>(port, obj));
    }
    pthread_rwlock_unlock(&rwlock);

    pthread_rwlock_rdlock(&rwlock);
    for (it=servers.begin(); it!=servers.end(); ++it) {
        pobj = &(it->second);
        if((pobj->init() != 0) || ((ret=pthread_create(&(pobj->thrId), NULL, tcpSvrManageThrdProc, (void*)pobj))!=0)) {
            pthread_rwlock_unlock(&rwlock);
            goto end;
        }        
    }
    pthread_rwlock_unlock(&rwlock);
    ELOG(L_INFO,"run tcp server successful");

    while(!g_stopflag) {
        sleep(30);
        pthread_rwlock_rdlock(&rwlock);
        ELOG(L_INFO,"check all tcp server(size=%u)", servers.size());

        for (it=servers.begin(); it!=servers.end(); ++it) {
            pobj = &(it->second);
            DEBUG_PRINT("[%s][%d]:[%s] re_run tcp server ining(port=%d, thrId=%llu)\n", it->first, pobj->thrId);
            if (pobj->runFlag == 2) {
                if(pobj->thrId != 0) {
                    ELOG(L_INFO,"Reclaim tcp server thread resources (listen_ip=0x%08X, listen_port=%u, listen_socket_type=%u, remote_ip=0x%08X, remote_port=%u, remote_socket_type=%u)",
                        pobj->confInfo.listen_ip, pobj->confInfo.listen_port, pobj->confInfo.listen_socket_type, pobj->confInfo.remote_ip, pobj->confInfo.remote_port, pobj->confInfo.remote_socket_type);  
                    pthread_join(pobj->thrId, NULL);
                    pobj->thrId = 0;
                }
                 
                ret=pthread_create(&(pobj->thrId), NULL, tcpSvrManageThrdProc, (void*)pobj);
                ELOG(L_INFO,"re_run tcp server(port=%d, ret=%d, %llu)", it->first, ret, pobj->thrId);
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
                pthread_join(pobj->thrId, NULL);
            }
        }
    }
    sleep(5);    
    servers.clear();
    ELOG(L_INFO,"run tcp server failed(errCode=%d)", ret);
    return -1;
}




