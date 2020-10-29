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
#include <stdarg.h>
#include <assert.h>
#include <map>
#include <stdbool.h>
#include "utils.h"
#include "sgg_comm.h"
#include "sgg_audit.h"
#include "sgg_manage.h"

pthread_t g_testclientpktrecvthrd = 0;
pthread_t g_testudpclientpktrecvthrd = 0;

int g_teststopflag = 0;
#define TCP_TEST_BUFF_LEN 32768
static void* thread_tcp_clientpktrecv(void *arg) {
	int sd = *((int*)arg);
    int rv = 0;
    char buf[TCP_TEST_BUFF_LEN];

    printf("thread_tcp_clientpktrecv in...\r\n");
    
    while(1){
        rv = recv(sd, buf, TCP_TEST_BUFF_LEN, 0);
        if (rv > 0) {
            printf("***[thread_tcp_clientpktrecv] succ:rv_len=%d,%s\r\n", rv,buf);
        } else {
            printf("***[thread_tcp_clientpktrecv] exit:rv_len=%d\r\n", rv);
            break;
        }
        pthread_testcancel();
    }
    printf("thread_tcp_clientpktrecv out...\r\n");
    return NULL;
}

int tcp_testclient(unsigned int ipaddr, unsigned int port, int times, int interval, int dataLen)
{
	int client_sd;
    int con_rv;
	int sendDataNum = -1; 
    char buf[TCP_TEST_BUFF_LEN]; 
    int temp = 0;
    struct timeval tv;
    struct timezone tz;
    (void)gettimeofday(&tv, &tz);
    

    struct sockaddr_in client_sockaddr; //定义IP地址结构
    client_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sd == -1) {
        return -1;
    }
    memset(&client_sockaddr, 0, sizeof(client_sockaddr));
    client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
    client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
    client_sockaddr.sin_addr.s_addr = htonl(ipaddr); //将字符串的ip地址转换成int型,客服端要连接的ip地址
    con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr, sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
    if (con_rv == -1) {
        close(client_sd);
        return -2;
    }
    if (pthread_create(&g_testclientpktrecvthrd, NULL, thread_tcp_clientpktrecv, &client_sd) != 0) //创建组包上送线程
    {
        close(client_sd);
        return -3;
    }
    pthread_detach(g_testclientpktrecvthrd);


    for(int i=0;i<times;i++){
        memset(buf,0,TCP_TEST_BUFF_LEN);
        snprintf(buf, TCP_TEST_BUFF_LEN,"*SAILING---%08u---%04d---*", (unsigned int)tv.tv_sec, ++temp);
        
		sendDataNum = send(client_sd, buf, dataLen, 0);
        printf("===[thread_tcp_clientpktsend] succ:sd_len=%d,%s\r\n", sendDataNum,buf);
		if (sendDataNum == -1) {
			return -3;
		} 
		usleep(interval);
	}
    g_teststopflag = 1;
    sleep(1);
    pthread_cancel(g_testclientpktrecvthrd);
    printf("tcp test client socket exit...\r\n");
	close(client_sd);
	return 0;
}

int serverDynamicOp(unsigned int ipaddr, unsigned int port, unsigned int oper, unsigned int location, unsigned int localip, unsigned int localport,unsigned int localpro,
                    unsigned int remoteip, unsigned int remoteport,unsigned int remotepro, unsigned int num_s,unsigned int rtpauditflag, unsigned int rtpwhiteflag, unsigned int rtpwhiteinterval, unsigned int rtpwhitesize)
{
	int client_sd;
    int con_rv;
	int sendDataNum = -1; 
    int recvDataNum = -1;
    char buf[TCP_TEST_BUFF_LEN]; 
    time_t tt;
    struct tm tm_time;
    char sdtime[32] = {0};

    struct sockaddr_in client_sockaddr; //定义IP地址结构
    client_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sd == -1) {
        return -1;
    }
    memset(&client_sockaddr, 0, sizeof(client_sockaddr));
    client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
    client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
    client_sockaddr.sin_addr.s_addr = htonl(ipaddr); //将字符串的ip地址转换成int型,客服端要连接的ip地址
    con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr, sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
    if (con_rv == -1) {
        close(client_sd);
        return -2;
    }

    memset(buf,0,TCP_TEST_BUFF_LEN);
    SggCommMsgInfo *phead = (SggCommMsgInfo*)buf;
    SvrObjCommInfo *pObjCommMsg = NULL;
    SvrObjBaseInfo *pBody = NULL;
    if (oper==1) {
        pObjCommMsg = (SvrObjCommInfo*)(phead+1);
        pBody = (SvrObjBaseInfo*)(pObjCommMsg+1);
        pObjCommMsg->location = location;
        pObjCommMsg->udp_send_interval = 0;
        pObjCommMsg->udp_send_heart_beat = 1;
        pObjCommMsg->tcp_buff_max_count = 128;
        pObjCommMsg->udp_buff_max_count = 128;
        pObjCommMsg->sgg_multi_lines = 10;
        phead->head.msgType = MSG_TYPE_ADD_SERVER_REQ;
        phead->head.msgLen = sizeof(SggCommMsgInfo)+sizeof(SvrObjCommInfo);
    } else {
        pBody = (SvrObjBaseInfo*)(phead+1);
        phead->head.msgType = MSG_TYPE_RMV_SERVER_REQ;
        phead->head.msgLen = sizeof(SggCommMsgInfo);
    }
    phead->head.ackFlag = 1;
    phead->head.errCode = 0;
    
    phead->head.version = 100;
    int temp = localport;
    
    
    for(unsigned int i=0; i<num_s;i++) {
        pBody->local.ip = localip;
        pBody->local.port = temp++;
        pBody->local.protocol = localpro;

        pBody->remote.ip = remoteip;
        pBody->remote.port = remoteport+i;
        pBody->remote.protocol = remotepro;
        pBody->rtpCfgFlag[0] = rtpauditflag;
        pBody->rtpCfgFlag[1] = rtpwhiteflag;
        pBody->rtpCfgFlag[2] = rtpwhiteinterval;
        pBody->rtpCfgFlag[3] = rtpwhitesize;
        pBody++;        
    }
    phead->head.msgLen += (sizeof(SvrObjBaseInfo)*num_s); 

    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time); //日志的时间  
	sendDataNum = send(client_sd, buf, phead->head.msgLen, 0);
    printf("===[serverDynamicOp][%s] succ:sd_len=%d,%s\r\n", sdtime, sendDataNum,buf);
    memset(buf,0,TCP_TEST_BUFF_LEN);
    phead = (SggCommMsgInfo*)buf;
    recvDataNum = recv(client_sd, buf, TCP_TEST_BUFF_LEN, 0);

    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time); //日志的时间  
    
    if(recvDataNum>0){
    printf("[%s]msgType=%u\r\nerrCode=%u\r\nmsgLen=%u\r\nversion=%u\r\n", sdtime, phead->head.msgType, phead->head.errCode, phead->head.msgLen, phead->head.version);
    } else {
        printf("recv nothing\r\n");
    }
    
    printf("tcp test client socket exit...\r\n");
    //sleep(2);
	close(client_sd);
	return 0;
}


pthread_t g_testserverpktrecvthrd = 0;
pthread_t g_testudpserverpktrecvthrd = 0;

static void* thread_tcp_serverpktrecv(void *arg) {
	int sd = *((int*)arg);
	char buf[TCP_TEST_BUFF_LEN];
	int rv_len = 0, sd_len=0;
	free(arg);
	
	while(1){
		rv_len = recv(sd, buf, TCP_TEST_BUFF_LEN, 0);
		if(rv_len > 0){
            sd_len = send(sd, buf, rv_len, 0);
			printf("***[thread_tcp_serverpktrecv] succ: rv_len=%d,sd_len=%d, %s\r\n",rv_len, sd_len, buf);      
		} else if(rv_len==0){
			printf("***[thread_tcp_serverpktrecv] exit: rv=%d,errno=%d\r\n", rv_len,errno);
            break;
		} else {
			printf("***[thread_tcp_serverpktrecv] failed: rv=%d,errno=%d\r\n", rv_len,errno);
            break;
		}
		pthread_testcancel();
	}
    close(sd);
    printf("============END==========\r\n");
	pthread_exit((void *) 0);	
	return NULL;
}

int tcp_testserver(unsigned int ipaddr, unsigned int port)
{
	int socket_id;
	struct sockaddr_in sockaddr; //定义IP地址结构
	int on = 1;
	int accept_st;
    struct sockaddr_in accept_sockaddr; //定义accept IP地址结构
    socklen_t addrlen = sizeof(accept_sockaddr);
    memset(&accept_sockaddr, 0, addrlen);

    DEBUG_PRINT("tcp_testserver in(0x%08X, %d)...\n", ipaddr, port);
	
	socket_id = socket(AF_INET, SOCK_STREAM, 0); //初始化socket
	if (socket_id == -1) {
		printf("[create_tcplisten] create tcp server socket failed:%s \n", strerror(errno));
		return -1;
	}

    // 查看系统默认的socket接收缓冲区大小
    int defRcvBufSize = -1;
    socklen_t optlen = sizeof(defRcvBufSize);
    if (getsockopt(socket_id, SOL_SOCKET, SO_RCVBUF, &defRcvBufSize, &optlen) < 0) {
        printf("[create_tcplisten] getsockopt error=%d(%s)!!!\n", errno, strerror(errno));
        return -1;
    }
   
	if (setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) //设置ip地址可重用
	{
		printf("[create_tcplisten] setsockopt error:%s \n", strerror(errno));
		return -1;
	}

    /*struct timeval timeout = {1,0};  
    if(setsockopt(socket_id, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
    {
        DEBUG_PRINT("[create_tcpsocket] setsockopt error:%s \n", strerror(errno));
		return -1;
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
		printf("[create_tcplisten] bind error:%s \n", strerror(errno));
		return -1;
	}

    if (listen(socket_id, 5) == -1) { //    服务端开始监听
        printf("[create_tcplisten] listen error:%s \n", strerror(errno));
        return -1;
    }
	
	do{
		accept_st = accept(socket_id, (struct sockaddr*) &accept_sockaddr,&addrlen);
		//accept 会阻塞直到客户端连接连过来 服务端这个socket只负责listen 
		// 是不是有客服端连接过来了,是通过accept返回socket通信的
		if (accept_st == -1)
		{
			printf("[accept_socket]accept error:%s \n", strerror(errno));
			return -1;
		}
		/*struct timeval timeout = {1,0};  
		if(setsockopt(accept_st, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
		{
			printf("[accept_socket] setsockopt error:%s \n", strerror(errno));
			return -1;
		}*/
		int *psd = (int*)malloc(4);
		*psd = accept_st;
		if (pthread_create(&g_testserverpktrecvthrd, NULL, thread_tcp_serverpktrecv, psd) != 0) //创建组包上送线程
		{
			printf("create pktsend thread error:%s \n", strerror(errno));
			return -2;
			break;
		}
		pthread_detach(g_testserverpktrecvthrd);
	} while(1);
	return 0;
}

/*static void* thread_udp_clientpktrecv(void *arg) {
	int sd = *((int*)arg);
    int rv = 0;
    char buf[TCP_TEST_BUFF_LEN];

    printf("thread_udp_clientpktrecv in...\r\n");
    
    while(1){
        rv = recv(sd, buf, TCP_TEST_BUFF_LEN, 0);
        if (rv > 0) {
            printf("***[thread_udp_clientpktrecv] succ:rv_len=%d,%s\r\n", rv,buf);
        } else {
            printf("***[thread_udp_clientpktrecv] exit:rv_len=%d\r\n", rv);
            break;
        }
        pthread_testcancel();
    }
    printf("thread_udp_clientpktrecv out...\r\n");
    return NULL;
}*/


static void* thread_udp_serverpktrecv(void *arg) {
	int sd = *((int*)arg);
	char buf[TCP_TEST_BUFF_LEN];
	int rv_len = 0, sd_len=0;
	free(arg);
	
	while(1){
		rv_len = recv(sd, buf, TCP_TEST_BUFF_LEN, 0);
		if(rv_len > 0){
            sd_len = send(sd, buf, rv_len, 0);
			printf("***[thread_udp_serverpktrecv] succ: rv_len=%d,sd_len=%d, %s\r\n",rv_len, sd_len, buf);      
		} else if(rv_len==0){
			printf("***[thread_udp_serverpktrecv] exit: rv=%d,errno=%d\r\n", rv_len,errno);
            break;
		} else {
			printf("***[thread_udp_serverpktrecv] failed: rv=%d,errno=%d\r\n", rv_len,errno);
            break;
		}
		pthread_testcancel();
	}
    close(sd);
    printf("============END==========\r\n");
	pthread_exit((void *) 0);	
	return NULL;
}

int udp_testclient(unsigned int ipaddr, unsigned int port, int times, int interval, int dataLen)
{
	int client_sd;
	int con_rv;
	struct sockaddr_in client_sockaddr; //定义IP地址结构
	socklen_t addrlen = sizeof(client_sockaddr);
    char buf[TCP_TEST_BUFF_LEN]; 
    int temp = 0;
    int sendDataNum = -1; 
    struct timeval tv;
    struct timezone tz;
    (void)gettimeofday(&tv, &tz);
	
	client_sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (client_sd == -1) {
		DEBUG_PRINT("create udp send client error:%s \n", strerror(errno));
		return -1;
	}
	memset(&client_sockaddr, 0, sizeof(client_sockaddr));
	client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
	client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
	client_sockaddr.sin_addr.s_addr = htonl(ipaddr);//将字符串的ip地址转换成int型,客服端要连接的ip地址
	con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr,sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
	if (con_rv == -1) {
		DEBUG_PRINT("udp connect error:%s \n", strerror(errno));
		return -1;
	}

    /*struct timeval timeout = {1,0};  
    if(setsockopt(client_sd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
    {
        DEBUG_PRINT("[run_tcpclient] setsockopt(SO_RCVTIMEO) error:%s \n", strerror(errno));
        return -ERR_TCP_SOCKET_SET_OPT_FAILED;
    }*/
    getsockname(client_sd, (struct sockaddr*) &client_sockaddr, &addrlen);
    
    DEBUG_PRINT("create udp send client succesful: sid=%d\r\n", client_sd);

    
    /*if (pthread_create(&g_testudpclientpktrecvthrd, NULL, thread_udp_clientpktrecv, &client_sd) != 0) //创建组包上送线程
    {
        close(client_sd);
        return -3;
    }
    pthread_detach(g_testudpclientpktrecvthrd);*/


    for(int i=0;i<times;i++){
        memset(buf,0,TCP_TEST_BUFF_LEN);
        snprintf(buf, TCP_TEST_BUFF_LEN,"*SAILING---%08u---%04d---*", (unsigned int)tv.tv_sec, ++temp);
        
		sendDataNum = send(client_sd, buf, dataLen, 0);
        printf("===[thread_tcp_clientpktsend] succ:sd_len=%d,%s\r\n", sendDataNum,buf);
		if (sendDataNum == -1) {
			return -3;
		} 
		usleep(interval);
	}
    g_teststopflag = 1;
    sleep(1);
    //pthread_cancel(g_testudpclientpktrecvthrd);
    printf("udp test client socket exit...\r\n");
	close(client_sd);
	return 0;
}


int udp_testserver(unsigned int ipaddr, unsigned int port)
{
    int socket_id;
	struct sockaddr_in sockaddr; //定义IP地址结构
	int on = 1;

    DEBUG_PRINT("udp_testserver in(0x%08X, %d)...\n", ipaddr, port);
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
    DEBUG_PRINT("[%s][%d]: OS default udp socket recv buff size is: %d,%d\n", __FILE__, __LINE__,defRcvBufSize, optlen);


	if (setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) //设置ip地址可重用
	{
		DEBUG_PRINT("setsockopt error:%s \n", strerror(errno));
		return -1;
	}

    /*struct timeval timeout = {1,0};  
    if(setsockopt(socket_id, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval)) ==-1) 
    {
        DEBUG_PRINT("setsockopt error:%s \n", strerror(errno));
		return -1;
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
		DEBUG_PRINT("bind error:%s \n", strerror(errno));
		return -1;
	}
            
    int *psd = (int*)malloc(4);
    *psd = socket_id;
    if (pthread_create(&g_testudpserverpktrecvthrd, NULL, thread_udp_serverpktrecv, psd) != 0) //创建组包上送线程
    {
        printf("create pktsend thread error:%s \n", strerror(errno));
        return -2;
    }
    pthread_join(g_testudpserverpktrecvthrd, NULL);

    return 0;
}


int pktAuditProc(unsigned int ipaddr, unsigned int port, unsigned int protocal)
{
	int client_sd;
    int con_rv;
	int sendDataNum = -1; 
    int recvDataNum = -1;
    char buf[TCP_TEST_BUFF_LEN]; 
    int count = 0;
    sgg_audit_node *pauditBody = NULL;
    time_t tt;
    struct tm tm_time;
    char sdtime[32] = {0};

    struct sockaddr_in client_sockaddr; //定义IP地址结构
    client_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sd == -1) {
        return -1;
    }
    memset(&client_sockaddr, 0, sizeof(client_sockaddr));
    client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
    client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
    client_sockaddr.sin_addr.s_addr = htonl(ipaddr); //将字符串的ip地址转换成int型,客服端要连接的ip地址
    con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr, sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
    if (con_rv == -1) {
        close(client_sd);
        return -2;
    }

    memset(buf,0,TCP_TEST_BUFF_LEN);
    SggCommMsgInfo *phead = (SggCommMsgInfo*)buf;
    phead->head.msgType = MSG_TYPE_RTP_AUDIT_REQ;

    phead->head.ackFlag = 1;
    phead->head.errCode = 0;
    phead->head.msgLen = sizeof(SggCommMsgInfo);
    phead->head.version = 100;
    
    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time); //日志的时间  
	sendDataNum = send(client_sd, buf, phead->head.msgLen, 0);
    printf("===[pktAuditProc][%s] succ:sd_len=%d,%s\r\n", sdtime, sendDataNum,buf);
    memset(buf,0,TCP_TEST_BUFF_LEN);
    phead = (SggCommMsgInfo*)buf;
    recvDataNum = recv(client_sd, buf, TCP_TEST_BUFF_LEN, 0);

    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time); //日志的时间  
    
    if(recvDataNum>0){
        printf("[%s]msgType=%u\r\nerrCode=%u\r\nmsgLen=%u\r\nversion=%u\r\n", sdtime, phead->head.msgType, phead->head.errCode, phead->head.msgLen, phead->head.version);
        count = (phead->head.msgLen - sizeof(SggCommMsgInfo))/sizeof(sgg_audit_node);
        pauditBody = (sgg_audit_node*)phead->buf;
        for(int i=0; i<count;i++) {
            printf("0x%08X-%05u-0x%08X-%05u-%u-%s-%s-%08llu\r\n", pauditBody->pktInfo.srcIP, pauditBody->pktInfo.srcPort,pauditBody->pktInfo.dstIP,pauditBody->pktInfo.dstPort,pauditBody->pktInfo.protocol,
                pauditBody->startTime,pauditBody->endTime,pauditBody->pktStat);
            HexCodePrint((char*)pauditBody, sizeof(sgg_audit_node));
            pauditBody++;
            
        }
    } else {
        printf("recv nothing\r\n");
    }
    
    printf("tcp test client socket exit...\r\n");
    sleep(2);
	close(client_sd);
	return 0;
}


int pktenCodingProc(unsigned int ipaddr, unsigned int port, unsigned int protocal)
{
	int client_sd;
    int con_rv;
	int sendDataNum = -1; 
    int recvDataNum = -1;
    char buf[TCP_TEST_BUFF_LEN]; 
    int count = 0;
    sgg_rtp_encoding_node *penCodingBody = NULL;
    time_t tt;
    struct tm tm_time;
    char sdtime[32] = {0};

    struct sockaddr_in client_sockaddr; //定义IP地址结构
    client_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sd == -1) {
        return -1;
    }
    memset(&client_sockaddr, 0, sizeof(client_sockaddr));
    client_sockaddr.sin_port = htons(port); //指定一个端口号并将hosts字节型传化成Inet型字节型（大端或或者小端问题）
    client_sockaddr.sin_family = AF_INET; //设置结构类型为TCP/IP
    client_sockaddr.sin_addr.s_addr = htonl(ipaddr); //将字符串的ip地址转换成int型,客服端要连接的ip地址
    con_rv = connect(client_sd, (struct sockaddr*) &client_sockaddr, sizeof(client_sockaddr)); //调用connect连接到指定的ip地址和端口号,建立连接后通过socket描述符通信
    if (con_rv == -1) {
        close(client_sd);
        return -2;
    }

    memset(buf,0,TCP_TEST_BUFF_LEN);
    SggCommMsgInfo *phead = (SggCommMsgInfo*)buf;
    Pkt3Tuple *preqMsg = (Pkt3Tuple*)(phead+1);
    phead->head.msgType = MSG_TYPE_RTP_CODING_GET_REQ;

    phead->head.ackFlag = 1;
    phead->head.errCode = 0;
    phead->head.msgLen = sizeof(SggCommMsgInfo)+sizeof(Pkt3Tuple);
    phead->head.version = 100;

    preqMsg->ip = 0xffffffff;
    preqMsg->port = 0xffffffff;
    preqMsg->protocol = 0;
    
    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time); //日志的时间  
	sendDataNum = send(client_sd, buf, phead->head.msgLen, 0);
    printf("===[pktenCodingProc][%s] succ:sd_len=%d,%s\r\n", sdtime, sendDataNum,buf);
    memset(buf,0,TCP_TEST_BUFF_LEN);
    phead = (SggCommMsgInfo*)buf;
    recvDataNum = recv(client_sd, buf, TCP_TEST_BUFF_LEN, 0);

    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time); //日志的时间  
    
    if(recvDataNum>0){
        printf("[%s]msgType=%u\r\nerrCode=%u\r\nmsgLen=%u\r\nversion=%u\r\n", sdtime, phead->head.msgType, phead->head.errCode, phead->head.msgLen, phead->head.version);
        count = (phead->head.msgLen - sizeof(SggCommMsgInfo))/sizeof(sgg_rtp_encoding_node);
        penCodingBody = (sgg_rtp_encoding_node*)phead->buf;
        for(int i=0; i<count;i++) {
            printf("0x%08X-%05u-0x%08X-%05u-%u-%u-%u-%u\r\n", penCodingBody->pktInfo.srcIP, penCodingBody->pktInfo.srcPort,penCodingBody->pktInfo.dstIP,penCodingBody->pktInfo.dstPort,penCodingBody->pktInfo.protocol,
                penCodingBody->enCapsulationFormat, penCodingBody->audio_type, penCodingBody->video_type);
            HexCodePrint((char*)penCodingBody, sizeof(sgg_rtp_encoding_node));
            penCodingBody++;
            
        }
    } else {
        printf("recv nothing\r\n");
    }
    
    printf("tcp test client socket exit...\r\n");
    sleep(2);
	close(client_sd);
	return 0;
}







