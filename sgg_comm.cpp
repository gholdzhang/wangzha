#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <map>
#include <vector>
#include "cJSON.h"
#include "threadpool.h"
#include "utils.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "log.h"
#include "sgg_err.h"
#include "sgg_comm.h"

#define COMM_SEND_TASK_MAX_NUM         100
#define COMM_SEND_TASK_MIN_NUM         30
#define COMM_RECV_TASK_MAX_NUM         100

typedef struct{
    int usedflag;
    int server_sid;
    int client_sid;
    int client_closewaite;
    int location;    
    int flag; /* 申请的线程池组标记 */
    int stop_flag;

    char inMsgBuf[PER_PACKET_MAX_SIZE];
    char outMsgBuf[PER_PACKET_MAX_SIZE];
}comm_thread_para;


std::map<unsigned int, fun_SggMsgProc> g_commMsgProc;

static int comm_AddServerMsgProc(void*inData, int inLen, void*outData, int* outLen) 
{
    SggCommMsgInfo*pInMsg = (SggCommMsgInfo*)inData;
    SggCommMsgInfo*pOutMsg = (SggCommMsgInfo*)outData;
    SvrObjCommInfo *pObjCommMsg = (SvrObjCommInfo*)(pInMsg+1);
    SvrObjBaseInfo *pBody = (SvrObjBaseInfo*)(pObjCommMsg+1);
    unsigned int errCode = ERR_SGG_OK;
    int count = 0;
    int retValue = 0;
    unsigned int temp = 0;

    if((pInMsg->head.msgLen < (sizeof(SggCommMsgInfo)+sizeof(SvrObjCommInfo))) || (pInMsg->head.msgLen != (unsigned int)inLen)) {
        errCode = ERR_SGG_INVALID_MSG_BODY;
        ELOG(L_ERROR,"add server msg len is invalid: msgLen=%d", pInMsg->head.msgLen);
        goto end;
    }
    count = (pInMsg->head.msgLen-sizeof(SggCommMsgInfo)-sizeof(SvrObjCommInfo))/sizeof(SvrObjBaseInfo);
    ELOG(L_INFO,"msgType=0x%08x, msgLen=%d, ackFlag=%u,version=%u,location=%u, multi_lines=%u, tcp_buff_max_count=%u, udp_buff_max_count=%u, udp_send_heart_beat=%u, udp_send_interval=%u, count=%d, inLen=%d",  
        pInMsg->head.msgType,pInMsg->head.msgLen, pInMsg->head.ackFlag, pInMsg->head.version,pObjCommMsg->location, pObjCommMsg->sgg_multi_lines,
        pObjCommMsg->tcp_buff_max_count,pObjCommMsg->udp_buff_max_count,pObjCommMsg->udp_send_heart_beat,pObjCommMsg->udp_send_interval,count,inLen);
    
    for(int i=0; i<count; i++) {
        //AuthInfoManageIns.updateStatus(pBody->value, pBody->operType);
        if (pBody->local.protocol == NET_TYPE_TCP) {
            retValue = TcpSvrManageInstance->add(pObjCommMsg, pBody);
        } else {
            retValue = UdpSvrManageInstance->add(pObjCommMsg, pBody);
        }

        temp = *((unsigned int*)&pBody->rtpCfgFlag);
        ELOG(L_INFO,"add server(srcIP=0x%08X, srcPort=%u, srcprotocol=%u, dstIP=0x%08X, dstPort=%u, dstprotocol=%u, rtpCfgFlag=0x%08X,retValue=%d).", 
            pBody->local.ip, pBody->local.port, pBody->local.protocol, pBody->remote.ip, pBody->remote.port, pBody->remote.protocol, temp,retValue);
        if (retValue != ERR_SGG_OK) {
            errCode = retValue;
            break;
        }
        pBody++;
    }

end:
    if(pInMsg->head.ackFlag == 0) {
        *outLen = 0;
    } else {
        *outLen = sizeof(SggCommMsgHead);
        pOutMsg->head.msgType = MSG_TYPE_ADD_SERVER_RESP;
        pOutMsg->head.msgLen = sizeof(SggCommMsgHead);
        pOutMsg->head.version = 100;
        pOutMsg->head.errCode = errCode;
        pOutMsg->head.ackFlag = 0;
    }
    
    return errCode;
}

static int comm_RmvServerMsgProc(void*inData, int inLen, void*outData, int* outLen) 
{
    SggCommMsgInfo*pInMsg = (SggCommMsgInfo*)inData;
    SggCommMsgInfo*pOutMsg = (SggCommMsgInfo*)outData;
    SvrObjBaseInfo *pBody = (SvrObjBaseInfo*)(pInMsg+1);
    unsigned int errCode = ERR_SGG_OK;
    int count = 0;
    int retValue = 0;
    if((pInMsg->head.msgLen < sizeof(SggCommMsgInfo)) || (pInMsg->head.msgLen != (unsigned int)inLen)) {
        errCode = ERR_SGG_INVALID_MSG_BODY;
        ELOG(L_ERROR,"msg len invalid msgLen=%d, realLen=%d", pInMsg->head.msgLen, inLen);
        goto end;
    }
    count = (pInMsg->head.msgLen-sizeof(SggCommMsgInfo))/sizeof(SvrObjBaseInfo);
    ELOG(L_INFO,"msgType=0x%08x, msgLen=%d, ackFlag=%u,version=%u,count=%d, inLen=%d",  
        pInMsg->head.msgType,pInMsg->head.msgLen, pInMsg->head.ackFlag, pInMsg->head.version,count, inLen);
    if(pBody->local.protocol == NET_TYPE_TCP) {
        /*for(int i=0; i<count; i++) {
            //AuthInfoManageIns.updateStatus(pBody->value, pBody->operType);       
            retValue = TcpSvrManageInstance->rmv(pBody); 
            ELOG(L_INFO,"rmv tcp server(srcIP=0x%08X, srcPort=%u, srcprotocol=%u, dstIP=0x%08X, dstPort=%u, dstprotocol=%u, retValue=%d).", 
                pBody->local.ip, pBody->local.port, pBody->local.protocol, pBody->remote.ip, pBody->remote.port, pBody->remote.protocol, retValue);
            pBody++;
        }*/
        retValue = TcpSvrManageInstance->rmv(pBody, count);
        ELOG(L_INFO,"rmv tcp server(count=%u, retValue=%d).", count, retValue);
        errCode = retValue;
    } else {
        retValue = UdpSvrManageInstance->rmv(pBody, count);
        ELOG(L_INFO,"rmv udp server(count=%u, retValue=%d).", count, retValue);
        errCode = retValue;
    }   

end:
    if(pInMsg->head.ackFlag == 0) {
        *outLen = 0;
    } else {
        *outLen = sizeof(SggCommMsgHead);
        pOutMsg->head.msgType = MSG_TYPE_RMV_SERVER_RESP;
        pOutMsg->head.msgLen = sizeof(SggCommMsgHead);
        pOutMsg->head.version = 100;
        pOutMsg->head.errCode = errCode;
        pOutMsg->head.ackFlag = 0;
    }
    
    return errCode;
}


int comm_registerMsg(unsigned int msgId, fun_SggMsgProc pfun)
{
    std::pair<std::map<unsigned int, fun_SggMsgProc>::iterator, bool> ret;
    ret = g_commMsgProc.insert(std::pair<unsigned int, fun_SggMsgProc>(msgId,pfun));
    if (ret.second==false) {
        ELOG(L_ERROR,"Message(0x%08X) registration failed.", msgId);
        return ERR_SGG_ERR;
    }
    return ERR_SGG_OK;
}

fun_SggMsgProc comm_getMsgProcFun(unsigned int msgId)
{
    std::map<unsigned int, fun_SggMsgProc>::iterator it = g_commMsgProc.begin();
    for (; it!=g_commMsgProc.end(); ++it) {
        if(it->first==msgId) {
            return it->second;
        }
    }
    return NULL;
}


int comm_serverMsgProc(void*inData, int inLen, void*outData, int* outLen)
{
    int retValue = 0;
    MsgQueueInfo*inMsg = (MsgQueueInfo*)inData;

    DEBUG_PRINT("[%s][%d]:[%s] recv message type=0x%08X\r\n", __FILE__, __LINE__, __FUNCTION__, inMsg->head.msgType);
    fun_SggMsgProc pfun = comm_getMsgProcFun(inMsg->head.msgType);
    if(pfun != NULL) {
        retValue = pfun(inData, inLen, outData, outLen);
    } else {
        ELOG(L_ERROR,"invalid message type=0x%08X", inMsg->head.msgType);
        retValue = ERR_SGG_INVALID_MSG_TYPE;
    }
    return retValue;
}

void *comm_threadmsgproc(void *arg1, void *arg2, void*arg3)
{
    int retValue = ERR_SGG_OK;
    int ret = 0;
    unsigned int rcvedLen = 0;
    int outLen = 0;
    int sdLen = 0;
    int overLen = -1;
    int offset = 0;
    comm_thread_para *para=(comm_thread_para*)arg1;
    char *pbuf = para->inMsgBuf;
    SggCommMsgHead *phead = (SggCommMsgHead*)para->inMsgBuf;
    bzero(para->inMsgBuf, PER_PACKET_MAX_SIZE);
    
    while(1) {        
        ret = recv(para->server_sid, pbuf, PER_PACKET_MAX_SIZE,0);
        if (ret <= 0) {
            goto end;
        } else {
            //HexCodePrint((void*)para->inMsgBuf, ret);
            rcvedLen += ret;
            CONTINUE_IF(phead->msgLen > rcvedLen);
            outLen = PER_PACKET_MAX_SIZE;
            bzero(para->outMsgBuf, PER_PACKET_MAX_SIZE);
            retValue = comm_serverMsgProc(para->inMsgBuf, rcvedLen, para->outMsgBuf, &outLen);
            if(outLen > 0) {
                overLen = outLen;
                offset = 0;
            
                if(outLen <= PER_PACKET_MAX_SIZE) {
                    pbuf = para->outMsgBuf;
                } else {
                    pbuf = (char*)(*((size_t*)(para->outMsgBuf)));
                }

                do{
                    sdLen = send(para->server_sid,(void*)(pbuf+offset), overLen,0);
                    if(sdLen>0) {
                        overLen -= sdLen;
                        offset += sdLen;
                    }
                }while(sdLen>0 && overLen>0); 


                EXEC_IF((outLen > PER_PACKET_MAX_SIZE), free(pbuf));

                if (overLen>0){
                    ELOG(L_ERROR,"tcp send error:sid=%d, dataLen=%d, need_send=%d, err=%d, %s", 
                        para->server_sid, outLen, overLen, errno, strerror(errno)); 
                    break;
                }
                
            }
            if (retValue != ERR_SGG_OK) {
                ELOG(L_ERROR,"proc message failed(errCode=%d)", retValue);
            }
        }       
    }

end:
    para->usedflag = 0;
    close(para->server_sid);
    para->server_sid = -1;
    return NULL;
}


serverCommObj::serverCommObj(unsigned int ip, unsigned int port, unsigned protocol)
{
    socketCfg.ip = ip;
    socketCfg.port = port;
    socketCfg.protocol = protocol;
    ELOG(L_INFO,"new comm server(0x%08X, %u, %u).", socketCfg.ip, socketCfg.port, socketCfg.protocol);
}

serverCommObj::~serverCommObj()
{
    ELOG(L_INFO,"delete comm server(0x%08X, %u, %u).", socketCfg.ip, socketCfg.port, socketCfg.protocol);
}


int serverCommObj::run(void)
{
    int accept_st = -1;
    int socket_id = -1;
    int retValue = 0;
    int i = 0;
    threadpool_t *thrdPools = NULL;
    comm_thread_para *bufMap=NULL;
    
    socket_id = create_tcplisten(socketCfg.ip, socketCfg.port, TCP_PROTOCAL_BUFF_MAX_SIZE); //创建监听socket
    if (socket_id < 0) {
        retValue =  -1;
        goto end;
    }

    /* 屏蔽SIGPIPE信号 */
    sigdismiss_tcpsocket();

    bufMap = (comm_thread_para*)malloc(sizeof(comm_thread_para)*(COMM_SEND_TASK_MAX_NUM+1));
    if (bufMap == NULL) {
        ELOG(L_INFO,"Allocate memory failed.");
        goto end;
    }
    memset(bufMap, 0, sizeof(comm_thread_para)*(COMM_SEND_TASK_MAX_NUM+1));

    thrdPools = threadpool_create(COMM_SEND_TASK_MIN_NUM, COMM_SEND_TASK_MAX_NUM, COMM_RECV_TASK_MAX_NUM, socketCfg.port);
    if (thrdPools == NULL) {
        ELOG(L_INFO,"Create thread pools failed.");        
        goto end;
    }
    
    do {
        accept_st = accept_socket(socket_id, false); //获取连接的的socket
        if (accept_st == -1) {
            retValue =  -ERR_TCP_SOCKET_ACCEPT_FAILED;
            ELOG(L_WARNING,"tcp socket accept failed(ip=0x%08X, port=%u,asid=%d)", socketCfg.ip, socketCfg.port, accept_st);
            goto end;
        }
        do {           
            BREAK_IF(bufMap[i].usedflag == 0);
            i++;
            i = (i%(COMM_SEND_TASK_MAX_NUM+1));
        }while(1);

        bufMap[i].usedflag = 1;
        bufMap[i].server_sid = accept_st;
        
        threadpool_add_task(thrdPools, comm_threadmsgproc,  &bufMap[i], NULL, NULL);

        i++;
        i = (i%(COMM_SEND_TASK_MAX_NUM+1));  
    }while(1);

    end:

    (void)threadpool_destroy(thrdPools);
    thrdPools = NULL;
    free(bufMap);
    close(socket_id);

    return retValue;
}


static void* sgg_commThreadProc(void* arg)
{
    int retValue = ERR_SGG_OK;
    Pkt3Tuple *pserverInfo = (Pkt3Tuple*)arg;
    do {
        serverCommObj obj(pserverInfo->ip, pserverInfo->port, pserverInfo->protocol);
        retValue = obj.run();
        sleep(10);
    }while(ERR_SGG_OK != retValue);
    free(arg); 
    pthread_exit((void *) 0);
    return NULL;
}

//4398
void RunCommServer(Pkt3Tuple &serverInfo)
{
    pthread_t thread_id;
    Pkt3Tuple *para = NULL;
    static int flag = 0;
    RET_VOID_IF(flag != 0);
    flag = 1;
    para = (Pkt3Tuple*)malloc(sizeof(Pkt3Tuple));
    memcpy(para, &serverInfo, sizeof(Pkt3Tuple));

    comm_registerMsg(MSG_TYPE_ADD_SERVER_REQ, comm_AddServerMsgProc);
    comm_registerMsg(MSG_TYPE_RMV_SERVER_REQ, comm_RmvServerMsgProc);
   
    if (0 != pthread_create(&thread_id, NULL, sgg_commThreadProc, para)) {
       ELOG(L_ERROR,"comm server thread created failed.");
       free(para);
    } else {
        ELOG(L_INFO,"comm server thread created successfully.");
    }
    pthread_detach(thread_id);

    return;
}



