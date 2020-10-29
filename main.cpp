#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <map>
#include <syslog.h>
#include "cJSON.h"
#include "threadpool.h"
#include "utils.h"
#include "udp_socket.h"
#include "tcp_socket.h"
#include "sgg_manage.h"
#include "log.h"

extern int tcp_testclient(unsigned int ipaddr, unsigned int port, int times, int interval, int dataLen);
extern int udp_testclient(unsigned int ipaddr, unsigned int port, int times, int interval, int dataLen);
extern int tcp_testserver(unsigned int ipaddr, unsigned int port);
extern int udp_testserver(unsigned int ipaddr, unsigned int port);
extern int serverDynamicOp(unsigned int ipaddr, unsigned int port, unsigned int oper, unsigned int location, unsigned int localip, unsigned int localport,unsigned int localpro,
                    unsigned int remoteip, unsigned int remoteport,unsigned int remotepro, unsigned int num_s,unsigned int rtpauditflag, unsigned int rtpwhiteflag, unsigned int rtpwhiteinterval, unsigned int rtpwhitesize);
extern int pktAuditProc(unsigned int ipaddr, unsigned int port, unsigned int protocal);
extern int pktenCodingProc(unsigned int ipaddr, unsigned int port, unsigned int protocal);

#ifndef _DEBUG_OFF_
int socket_test(int cmd, unsigned int ipaddr, unsigned int port,  unsigned int count, int interval, unsigned int datalen)
{
    udp_server_manage *pUdpSvrM = NULL;

    switch(cmd){
        case 0:
            udp_testclient(ipaddr, port, count, interval, datalen);
            break;
        case 1:
            tcp_testclient(ipaddr, port, count, interval, datalen);            
            break;
        case 2:
            udp_testserver(ipaddr, port);            
            break;
        case 3: 
            tcp_testserver(ipaddr, port); 
            break;
        case 100:
            while(1){
                sgg_admin_process("./sgg_conf.json");
                sleep(1);
            }
            break;
        case 300:
            pktAuditProc(ipaddr, port, count);
            break;
        case 350:
            pktenCodingProc(ipaddr, port, count);
            break;
        default:
            return 0;
    }
    if(pUdpSvrM != NULL) {
        delete pUdpSvrM;
    }
    return 0;
}
#endif


int main(int argc,char*argv[])
{    
    #ifdef _DEBUG_OFF_
    if (argc < 2) {
        printf("%s", "sgg_app run failed: too few argument to application\n");
        syslog(LOG_ERR, "%s", "sgg_app run failed: too few argument to application\n");
        return -1;
    }
    
    if (file_exist_check(argv[1]) < 0) {       
        return -1;
    }
    
    while(1) {
        sgg_admin_process(argv[1]);
        sleep(1);
    }
    #else
    int cmd = 3;
    unsigned int ipaddr = 0x7f000001;
    unsigned int port = 7777;    
    unsigned int count = 10; 
    unsigned int interval = 1000000;
    unsigned int datalen = 100;
    if(argc<=1) {
        printf("sgg_app test process para invalid...\r\n");
        printf("[模拟运行TCP客户端]./sgg_app 1 remoteip remoteport sendtimes interval datalen(ex:./sgg_app 1 0x7f000001 7777 10 1000000 100)\r\n");
        printf("[模拟运行TCP服务端]./sgg_app 3 listenip listenport (ex:./sgg_app 3 0x7f000001 7777)\r\n");
        printf("[运行网闸或者光闸]./sgg_app 100 (ex:./sgg_app 100)\r\n"); 
        printf("[动态添加server]./sgg_app 200 ipaddr, port, oper,location, localip, localport,localpro, remoteip,remoteport,remotepro,num,rtpaudit,rtpWhite,wInterval,wSize \n");
        printf("[动态添加server]ex:./sgg_app 200 0x7f000001 4398 1 0 0x7f000001 1234 1 0x7f000001 4321 0 1 0 0 0 0)\r\n");
        printf("[模拟运行UDP报文审计]./sgg_app 300 ipaddr, port, protocal (ex: ./sgg_app 300 0x7f000001, 4392, 0)\r\n"); 
        printf("[模拟运行UDP编码封装格式审计]./sgg_app 350 ipaddr, port, protocal (ex: ./sgg_app 350 0x7f000001, 4392, 0)\r\n"); 
        return -1;
    }
    if (argc>1) cmd = atol(argv[1]);
    if (argc>2) ipaddr = htoi(argv[2]);
    if (argc>3) port = atol(argv[3]);
    if (argc>4) count = atol(argv[4]);
    if (argc>5) interval = atol(argv[5]);
    if (argc>6) datalen = atol(argv[6]);  
    
    if(cmd == 200) {
        if (argc<12) return -1;
        unsigned int num_s = 1;
        unsigned int rtpauditflag = 0;
        unsigned int rtpwhiteflag = 0;
        unsigned int rtpwhiteinterval = 0;
        unsigned int rtpwhitesize = 0;
        unsigned int ipaddr = htoi(argv[2]);
        unsigned int port = atol(argv[3]);
        unsigned int oper = atol(argv[4]);
        unsigned int location = htoi(argv[5]);
        unsigned int localip = htoi(argv[6]);
        unsigned int localport = atol(argv[7]);
        unsigned int localpro= atol(argv[8]);
        unsigned int remoteip =htoi(argv[9]);
        unsigned int remoteport= atol(argv[10]);
        unsigned int remotepro= atol(argv[11]);
        if(argc>12) num_s = atol(argv[12]);
        if(argc>13) rtpauditflag = atol(argv[13]);
        if(argc>14) rtpwhiteflag = atol(argv[14]);
        if(argc>15) rtpwhiteinterval = atol(argv[15]);
        if(argc>16) rtpwhitesize = atol(argv[16]);
        serverDynamicOp(ipaddr, port, oper,location, localip, localport,localpro, remoteip,remoteport,remotepro, num_s, rtpauditflag, rtpwhiteflag, rtpwhiteinterval, rtpwhitesize);
        
    } else {
        printf("cmd=%d, ipaddr=0x%08x, port=%d, count=%d, interval=%d, datalen=%d\n", cmd, ipaddr, port, count, interval, datalen);
        socket_test(cmd, ipaddr, port, count, interval, datalen);
    }
    #endif
    
    return 0;
}



