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
#include "udp_socket.h"
#include "tcp_socket.h"
#include "tcp_socketprox.h"
#include "sgg_manage.h"
#include "log.h"
#include "sgg_err.h"
#include "sgg_comm.h"
#include "sgg_audit.h"

#define SGG_CONF_FILENAME   "sgg_conf.json"

#define SGG_CONF_KEY_REMOTE_ADDR "remote_addr"
#define SGG_CONF_KEY_LISTEN_ADDR "listen_addr"
#define SGG_CONF_KEY_REMOTE_ADDR_DOWN "remote_addr_down"
#define SGG_CONF_KEY_LISTEN_ADDR_DOWN "listen_addr_down"

#define SGG_CONF_KEY_LOCATION    "location"
#define SGG_CONF_KEY_IP "IP"
#define SGG_CONF_KEY_PORT "PORT"
#define SGG_CONF_KEY_PORT_BEGIN "PORT_BEGIN"
#define SGG_CONF_KEY_PORT_END "PORT_END"
#define SGG_CONF_KEY_NET_TYPE "NET_TYPE"
#define SGG_CONF_KEY_UDP_SEND_INTERVAL "udp_send_interval"
#define SGG_CONF_KEY_UDP_SEND_HB "udp_send_heart_beat"
#define SGG_CONF_KEY_TCP_BUFF_MAX_COUNT "tcp_buff_max_count"
#define SGG_CONF_KEY_UDP_BUFF_MAX_COUNT "udp_buff_max_count"
#define SGG_CONF_KEY_MULTI_LINES "sgg_multi_lines"
#define SGG_CONF_KEY_MODE "sgg_mode"
#define SGG_CONF_KEY_COMM_PORT "sgg_comm_port"



pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int g_stopflag = 0;
unsigned int g_multilines = 0;
Pktid2Socketid_s g_Pktid2SidInfo;
SggConfInfo g_sggConfInfo;
extern int g_udpHbFlag;




int get_runflag(void) {
    return g_stopflag;
}

void set_runflag(int f) {
    g_stopflag = f;
}

SggConfInfo* getSggConfInfo(void) {
    return &g_sggConfInfo;
}

void destroyTcpRecvQueue(TCPRecvDataQueue &queue, const unsigned int count)
{
    unsigned int i;
    for (i=0; i<count; i++) {
        if (queue.PktBufStack[i] != NULL) {
            DestroyStack(&queue.PktBufStack[i]);            
        }        
    }
    //pthread_cond_destroy(&(queue.cond));
    //pthread_mutex_destroy(&(queue.lock));
    return;
}

int createTcpRecvQueue(TCPRecvDataQueue &queue, const unsigned int count, const unsigned int size)
{
    unsigned int i;
    for (i=0; i<count; i++) {
        CONTINUE_IF(queue.PktBufStack[i] != NULL);
        CreateStack(&queue.PktBufStack[i], size, sizeof(SendPackStr));  
        if(queue.PktBufStack[i] == NULL) {
            goto end;
        }       
    }

    /*if (pthread_cond_init(&(queue.cond), NULL) != 0 || 
        pthread_mutex_init(&(queue.lock), NULL) != 0) {
        goto end;
    }*/

    queue.wpos = 0;
    queue.rpos = count-1;
    
    return 1;
end:
    destroyTcpRecvQueue(queue, count);
    return -1;
}



int checkTcpClientSocketAlive(int net_type, int flag, int pktdir, int remote_ip, int remote_port, int& sd, int stopFlag) {
    RET_IF(((sd >= 0) || (stopFlag==1)), 0);
    
    /*if ((net_type == NET_TYPE_TCP) && (sd > 0)) {
        getsockopt(sd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *)&len);
        RET_IF((info.tcpi_state == TCP_ESTABLISHED), 0);
        close(sd);
        LOG_OF_DAY("[%s][%d]:[%s] flag=%d, pktdir=%d, remote_ip=0x%08X,remote_port=%d, sd=%d, state=%d\n", __FILE__, __LINE__, __FUNCTION__, flag, pktdir, remote_ip,remote_port, sd, info.tcpi_state);
    }*/     
    if (net_type == NET_TYPE_TCP) {
        sd = run_tcpclient(remote_ip, remote_port, 0,true);
    } else {
        sd = run_udpclient(remote_ip, remote_port, 0);
    }
    RET_IF(sd<0, -1);
    ELOG(L_INFO,"flag=%d, pktdir=%d, remote_type[0:UDP,1:TCP]=%d, remote_ip=0x%08X,remote_port=%d, sd=%d", flag, pktdir, net_type, remote_ip,remote_port, sd);
    return 0;
}

int sgg_tcp_server_init(void) {
    pthread_rwlock_init(&(g_Pktid2SidInfo.rwlock),NULL);
    //g_Pktid2SidInfo.mapInfo.clear(); 
    if(g_Pktid2SidInfo.hbMapInfo != NULL) {
        delete g_Pktid2SidInfo.hbMapInfo;
        g_Pktid2SidInfo.hbMapInfo = NULL;
    }
    g_Pktid2SidInfo.hbMapInfo = new HashBucket<int>(g_multilines*2);
    return 0;
}

int getSocketIdByPktId(unsigned int pktid) {
    //std::map<unsigned int, int>::iterator it;
    int sd = -1;
    HNode<int>*node = NULL;
    
    pthread_rwlock_rdlock(&(g_Pktid2SidInfo.rwlock));
    /*it = g_Pktid2SidInfo.mapInfo.find(pktid);
    if (it != g_Pktid2SidInfo.mapInfo.end()) {
        sd = it->second;
    }*/
    node = g_Pktid2SidInfo.hbMapInfo->HashBucketSearch(pktid);
    if(node != NULL) {
        sd = node->data;
    }
    pthread_rwlock_unlock(&(g_Pktid2SidInfo.rwlock));
    return sd;
}

int insetSocketIdByPktId(unsigned int pktid, int sd) {
    int retVlaue = 0;
    //std::pair<std::map<unsigned int,int>::iterator,bool> ret;
    
    pthread_rwlock_wrlock(&(g_Pktid2SidInfo.rwlock));
    /*ret = g_Pktid2SidInfo.mapInfo.insert (std::pair<unsigned int,int>(pktid,sd));
    if (ret.second==false) {
        retVlaue = -1;
    }*/
    retVlaue = g_Pktid2SidInfo.hbMapInfo->HashBucketInsert(pktid, sd);
    pthread_rwlock_unlock(&(g_Pktid2SidInfo.rwlock));
    return retVlaue;
}

void deleteSocketIdByPktId(unsigned int pktid) {
    pthread_rwlock_wrlock(&(g_Pktid2SidInfo.rwlock));
    //g_Pktid2SidInfo.mapInfo.erase(pktid);
    g_Pktid2SidInfo.hbMapInfo->HashBucketRemove(pktid);
    pthread_rwlock_unlock(&(g_Pktid2SidInfo.rwlock));
    return ;
}

int sgg_read_conf(char* cfg_file, SggConfInfo &confInfo)
{
    FILE *f;//输入文件
    long len;//文件长度
    char *content;//文件内容

    f=fopen(cfg_file,"rb");
    if(f == NULL){
        DEBUG_PRINT("can not find config file:%s\n", SGG_CONF_FILENAME);  
        return -1;
    }
    fseek(f,0,SEEK_END);
    len=ftell(f);
    fseek(f,0,SEEK_SET);
    content=(char*)malloc(len+1);
    if(content == NULL) {
        DEBUG_PRINT("allocate memory failed:len=%u\n", len+1);  
        return -1;
    }
    fread(content,1,len,f);
    fclose(f); 

    memset(&confInfo, 0, sizeof(SggConfInfo));

    cJSON* cjson = cJSON_Parse(content);
    if (!cjson) {
        DEBUG_PRINT("Error before: [%s]\n",cJSON_GetErrorPtr());  
        return -1;
    }
    //printf("%s\r\n",cJSON_Print(cjson));    
    cJSON* test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_LISTEN_ADDR);
    
    cJSON*temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_IP);
    if (temp != NULL) {
        inet_pton(AF_INET,temp->valuestring, (void*)&((confInfo.listen_ip)));
        confInfo.listen_ip = ntohl(confInfo.listen_ip);
    }

    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT);
    if (temp != NULL) {
        confInfo.listen_port = temp->valueint;
    }
    
    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT_BEGIN);
    if (temp != NULL) {
        confInfo.listen_port_begin = temp->valueint;
    }

    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT_END);
    if (temp != NULL) {
        confInfo.listen_port_end = temp->valueint;
    } else {
        confInfo.listen_port_end = confInfo.listen_port_begin;
    }

    confInfo.listen_socket_type = NET_TYPE_UDP;
    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_NET_TYPE);    
    if((temp != NULL) && (strncasecmp(temp->valuestring, "TCP", 3) == 0)) {
        confInfo.listen_socket_type = NET_TYPE_TCP;
    } 
    
    DEBUG_PRINT("config:listen_ip=0x%08X, listen_port_begin=%u, listen_port_end=%u,listen_socket_type[0:UDP,1:TCP]=%u\n", confInfo.listen_ip, confInfo.listen_port_begin,confInfo.listen_port_end,confInfo.listen_socket_type);  

    
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_REMOTE_ADDR);
    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_IP);
    if (temp != NULL) {
        inet_pton(AF_INET,temp->valuestring, (void*)&((confInfo.remote_ip)));
        confInfo.remote_ip = ntohl(confInfo.remote_ip);
    }

    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT);
    if (temp != NULL) {
        confInfo.remote_port = temp->valueint;
    }
    
    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT_BEGIN);
    if (temp != NULL) {
        confInfo.remote_port_begin = temp->valueint;
    }

    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT_END);
    if (temp != NULL) {
        confInfo.remote_port_end = temp->valueint;
    } else {
        confInfo.remote_port_end = confInfo.remote_port_begin;
    }

    confInfo.remote_socket_type = NET_TYPE_UDP;
    temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_NET_TYPE);
    
    if((temp != NULL) && (strncasecmp(temp->valuestring, "TCP", 3) == 0)) {
        confInfo.remote_socket_type = NET_TYPE_TCP;
    } 
    
    DEBUG_PRINT("config:remote_ip=0x%08X, remote_port_begin=%u,remote_port_end=%u, remote_socket_type[0:UDP,1:TCP]=%u\n", confInfo.remote_ip, confInfo.remote_port_begin,confInfo.remote_port_end,confInfo.remote_socket_type);  

    /* 下行方向监听ip地址和端口解析 */  
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_LISTEN_ADDR_DOWN);
    if (test_arr != NULL) {        
        cJSON*temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_IP);
        if (temp != NULL) {
            inet_pton(AF_INET,temp->valuestring, (void*)&((confInfo.dlisten_ip)));
            confInfo.dlisten_ip = ntohl(confInfo.dlisten_ip);
        }

        temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT);
        if (temp != NULL) {
            confInfo.dlisten_port = temp->valueint;
        }

        confInfo.dlisten_socket_type = NET_TYPE_UDP;
        temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_NET_TYPE);    
        if((temp != NULL) && (strncasecmp(temp->valuestring, "TCP", 3) == 0)) {
            confInfo.dlisten_socket_type = NET_TYPE_TCP;
        } 

        DEBUG_PRINT("config:dlisten_ip=0x%08X, dlisten_port=%u, dlisten_socket_type[0:UDP,1:TCP]=%u\n", confInfo.dlisten_ip, confInfo.dlisten_port,confInfo.dlisten_socket_type);  
    }
    
    /* 下行方向远端ip地址和端口解析 */    
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_REMOTE_ADDR_DOWN);
    if(test_arr != NULL){
        temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_IP);
        if (temp != NULL) {
            inet_pton(AF_INET,temp->valuestring, (void*)&((confInfo.dremote_ip)));
            confInfo.dremote_ip = ntohl(confInfo.dremote_ip);
        }
        
        temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_PORT);
        if (temp != NULL) {
            confInfo.dremote_port = temp->valueint;
        }

        confInfo.dremote_socket_type = NET_TYPE_UDP;
        temp = cJSON_GetObjectItem(test_arr,SGG_CONF_KEY_NET_TYPE);    
        if((temp != NULL) && (strncasecmp(temp->valuestring, "TCP", 3) == 0)) {
            confInfo.dremote_socket_type = NET_TYPE_TCP;
        }     
        DEBUG_PRINT("config:dremote_ip=0x%08X, dremote_port=%u, dremote_socket_type[0:UDP,1:TCP]=%u\n", confInfo.dremote_ip, confInfo.dremote_port,confInfo.dremote_socket_type);  
    }

    confInfo.location = NET_LOCATION_CLIENT;
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_LOCATION);
    if((temp != NULL) && (strncasecmp(test_arr->valuestring, "SERVER", 6) == 0)) {
        confInfo.location = NET_LOCATION_SERVER;
    }

    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_UDP_SEND_INTERVAL);
    if (test_arr != NULL) {
        confInfo.udp_send_interval = test_arr->valueint;
    }

    confInfo.udp_send_hb = 1;
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_UDP_SEND_HB);
    if (test_arr != NULL) {
        confInfo.udp_send_hb = test_arr->valueint;
    }
    g_udpHbFlag = confInfo.udp_send_hb;

    confInfo.tcp_buff_max_count = TCP_BUFF_STACK_MAX_COUNT;
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_TCP_BUFF_MAX_COUNT);
    if (test_arr != NULL) {
        confInfo.tcp_buff_max_count = test_arr->valueint;
    }

    confInfo.udp_buff_max_count = UDP_BUFF_STACK_MAX_COUNT;
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_UDP_BUFF_MAX_COUNT);
    if (test_arr != NULL) {
        confInfo.udp_buff_max_count = test_arr->valueint;
    }

    confInfo.sgg_multi_lines = SGG_MULTI_LINES_DEFAULT_NUM;    
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_MULTI_LINES);
    if (test_arr != NULL) {
        confInfo.sgg_multi_lines = test_arr->valueint;
    }
    /*if(confInfo.sgg_multi_lines > SGG_MULTI_LINES_DEFAULT_NUM) {
        confInfo.sgg_multi_lines = SGG_MULTI_LINES_DEFAULT_NUM;
    }*/
    g_multilines = confInfo.sgg_multi_lines;

    confInfo.sgg_mode = SGG_MODE_0;
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_MODE);
    if (test_arr != NULL) {
        confInfo.sgg_mode = test_arr->valueint;
    }

    confInfo.sgg_comm_port = 4398;
    test_arr = cJSON_GetObjectItem(cjson,SGG_CONF_KEY_COMM_PORT);
    if (test_arr != NULL) {
        confInfo.sgg_comm_port = test_arr->valueint;
    }
    
    DEBUG_PRINT("config:location[0:CLIENT,1:SERVER]=%u, udp_send_interval=%uus, sgg_mode=%u, sgg_comm_port=%u\n", confInfo.location, confInfo.udp_send_interval, confInfo.sgg_mode, confInfo.sgg_comm_port);
    DEBUG_PRINT("config:tcp_buff_maxcount=%u, udp_buff_maxcount=%u, sgg_multi_lines=%u\n", confInfo.tcp_buff_max_count, confInfo.udp_buff_max_count, confInfo.sgg_multi_lines);

    cJSON_Delete(cjson);
    free(content);
    
    memcpy(&g_sggConfInfo, &confInfo,sizeof(g_sggConfInfo));
    return 0;
}

int sgg_admin_process(char *cfg_file)
{
    int retValue = -1;
    SggConfInfo confInfo;
    char prefix[512];
    char version[512] = "<NULL>";
#ifdef COMPILE_TIME
    strncpy(version,COMPILE_TIME, 512);
#endif  
    retValue = sgg_read_conf(cfg_file, confInfo);    
    if (retValue < 0) {
        return retValue;
    }
    /* 日志记录自研方法 */
    //snprintf(prefix, 512, "%d_%d_", confInfo.listen_socket_type, confInfo.listen_port_begin);
    //log_init(prefix);

    /* 日志记录easylogging++方法 */
    snprintf(prefix, 512, "./log/sgg_app_%d_%d.log", confInfo.listen_socket_type, confInfo.listen_port_begin);
    elog_init(prefix);
    
    ELOG(L_INFO,"sgg_admin_process(Build Time:%s) in\r", version);  
    ELOG(L_INFO,"config:location[0:CLIENT,1:SERVER]=%u, udp_send_interval=%uus, sgg_mode=%u, sgg_comm_port=%u", confInfo.location, confInfo.udp_send_interval, confInfo.sgg_mode, confInfo.sgg_comm_port);
    ELOG(L_INFO,"config:listen_ip=0x%08X, listen_port_begin=%u, listen_port_end=%u, listen_socket_type[0:UDP,1:TCP]=%u", confInfo.listen_ip, confInfo.listen_port_begin,confInfo.listen_port_end,confInfo.listen_socket_type);
    ELOG(L_INFO,"config:remote_ip=0x%08X, remote_port_begin=%u, remote_port_end=%u, remote_socket_type[0:UDP,1:TCP]=%u", confInfo.remote_ip, confInfo.remote_port_begin,confInfo.remote_port_end,confInfo.remote_socket_type);
    if (confInfo.sgg_mode == SGG_MODE_1) {
        ELOG(L_INFO,"config:listen_ip=0x%08X, listen_port=%u, listen_socket_type[0:UDP,1:TCP]=%u", 
            confInfo.dlisten_ip, confInfo.dlisten_port,confInfo.dlisten_socket_type);
        ELOG(L_INFO,"config:dremote_ip=0x%08X, dremote_port=%u, dremote_socket_type[0:UDP,1:TCP]=%u", 
            confInfo.dremote_ip, confInfo.dremote_port,confInfo.dremote_socket_type);
    }
    ELOG(L_INFO,"config:tcp_buff_maxcount=%u, udp_buff_maxcount=%u, sgg_multi_lines=%u", confInfo.tcp_buff_max_count, confInfo.udp_buff_max_count, confInfo.sgg_multi_lines);
    set_runflag(0);

    if(confInfo.sgg_mode == SGG_MODE_0) {
        Pkt3Tuple commSvr;
        commSvr.ip = confInfo.listen_ip;
        commSvr.port = confInfo.sgg_comm_port;
        commSvr.protocol = NET_TYPE_TCP;
        RunCommServer(commSvr);
        sgg_auditInit();
    }
    
    //tcp_server_manage *ptcpSvrM = NULL;
    //udp_server_manage *pudpSvrM = NULL;
    if (confInfo.sgg_mode == SGG_MODE_0) {        
        switch(confInfo.listen_socket_type) {
            case 0: //UDP
                //pudpSvrM = new udp_server_manage((void*)(&confInfo));
                UdpSvrManageInstance->init((void*)(&confInfo));
                retValue = UdpSvrManageInstance->run();
                break;
            case 1: //TCP
                /*ptcpSvrM = new tcp_server_manage((void*)(&confInfo));
                retValue = ptcpSvrM->run();
                if(retValue != 0) {
                    delete ptcpSvrM;
                }*/
                TcpSvrManageInstance->init((void*)(&confInfo));
                retValue = TcpSvrManageInstance->run();
                break;
            default://para invalid
                return -1;
        }
    } else if (confInfo.sgg_mode == SGG_MODE_1) {
        switch (confInfo.location) {
            case 0://正向代理，即location部署再client端
                sgg_tcp_server_init();
                retValue = runForwardProxyServer(NULL);
                break;
            case 1://反向代理，即location部署再server端
                sgg_tcp_server_init();
                retValue = runReverseProxyServer(NULL);
                break;
        }
    
    }
    return retValue;
}

