#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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
#include <dlfcn.h>
#include <map>
#include <set>
#include <queue>
#include "utils.h"
#include "udp_socket.h"
#include "tcp_socket.h"
#include "log.h"
#include "sgg_err.h"
#include "sgg_comm.h"
#include "sgg_audit.h"


sgg_audit_node::sgg_audit_node()
{
    memset(&pktInfo, 0, sizeof(Pkt5Tuple));
    pktStat = 0;
    memset(startTime, 0, TIME_STR_LEN);
    memset(endTime, 0, TIME_STR_LEN);
    memset(&detailInfo, 0, sizeof(AuditDetail));
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

sgg_audit_node::sgg_audit_node(const sgg_audit_node& obj)
{
    memcpy(&pktInfo, &(obj.pktInfo), sizeof(Pkt5Tuple));
    pktStat = obj.pktStat;
    memcpy(startTime, obj.startTime, TIME_STR_LEN);
    memcpy(endTime, obj.endTime, TIME_STR_LEN);
    memcpy(&detailInfo, &obj.detailInfo, sizeof(AuditDetail));
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

sgg_audit_node::~sgg_audit_node()
{
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

inline bool sgg_audit_node::operator==(const sgg_audit_node &obj) const
{
  if (this->pktInfo.srcIP==obj.pktInfo.srcIP &&
    this->pktInfo.srcPort==obj.pktInfo.srcPort &&
    this->pktInfo.dstIP==obj.pktInfo.dstIP &&
    this->pktInfo.dstPort==obj.pktInfo.dstPort &&
    this->pktInfo.protocol==obj.pktInfo.protocol)
     return true;
  return false;
}

sgg_audit_node& sgg_audit_node::operator= (const sgg_audit_node& obj)
{
    memcpy(&pktInfo, &(obj.pktInfo), sizeof(Pkt5Tuple));
    pktStat = obj.pktStat;
    memcpy(startTime, obj.startTime, TIME_STR_LEN);
    memcpy(endTime, obj.endTime, TIME_STR_LEN);
    memcpy(&detailInfo, &obj.detailInfo, sizeof(AuditDetail));
    return *this;
}


sgg_rtp_encoding_node::sgg_rtp_encoding_node()
{
    memset(&pktInfo, 0, sizeof(Pkt5Tuple));
    enCapsulationFormat = 0;
    audio_type = 0;
    video_type = 0;
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

sgg_rtp_encoding_node::sgg_rtp_encoding_node(const sgg_rtp_encoding_node& obj)
{
    memcpy(&pktInfo, &(obj.pktInfo), sizeof(Pkt5Tuple));
    enCapsulationFormat = obj.enCapsulationFormat;
    audio_type = obj.audio_type;   
    video_type = obj.video_type;
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

sgg_rtp_encoding_node::~sgg_rtp_encoding_node()
{
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

inline bool sgg_rtp_encoding_node::operator==(const sgg_rtp_encoding_node &obj) const
{
  if (this->pktInfo.srcIP==obj.pktInfo.srcIP &&
    this->pktInfo.srcPort==obj.pktInfo.srcPort &&
    this->pktInfo.dstIP==obj.pktInfo.dstIP &&
    this->pktInfo.dstPort==obj.pktInfo.dstPort &&
    this->pktInfo.protocol==obj.pktInfo.protocol)
     return true;
  return false;
}

sgg_rtp_encoding_node& sgg_rtp_encoding_node::operator= (const sgg_rtp_encoding_node& obj)
{
    memcpy(&pktInfo, &(obj.pktInfo), sizeof(Pkt5Tuple));
    enCapsulationFormat = obj.enCapsulationFormat;
    audio_type = obj.audio_type;  
    video_type = obj.video_type;
    return *this;
}


sgg_audit_manage* sgg_audit_manage::GetInstance()
{
    static sgg_audit_manage *instace = NULL;
    if(instace == NULL) {
        instace = new sgg_audit_manage;
    }
    return instace;
}
sgg_audit_manage::sgg_audit_manage()
{
    auditNodes.clear();
    pthread_rwlock_init(&rwlock,NULL);
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
}

sgg_audit_manage::~sgg_audit_manage()
{
    auditNodes.clear();
    pthread_rwlock_destroy(&rwlock);
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
    
}

int sgg_audit_manage::add(Pkt5Tuple &key, sgg_audit_node&obj, unsigned long long &totalLen)
{
    time_t tt;
    struct tm tm_time;
    char sdtime[32] = {0};    
    std::map<Pkt5Tuple, sgg_audit_node>::iterator it;

    tt = time(NULL);  
    localtime_r(&tt, &tm_time);  
    strftime(sdtime, sizeof(sdtime), "%Y-%m-%d %H:%M:%S", &tm_time);   
    
    pthread_rwlock_wrlock(&rwlock);
    it = auditNodes.find(key);
    if(it != auditNodes.end()) {
        memcpy(it->second.endTime, sdtime, TIME_STR_LEN);
        it->second.pktStat+= obj.pktStat;
        totalLen = it->second.pktStat;
    } else {
        memcpy(obj.startTime, sdtime, TIME_STR_LEN);
        memcpy(obj.endTime, sdtime, TIME_STR_LEN);
        auditNodes.insert(std::pair<Pkt5Tuple, sgg_audit_node>(key, obj));
        totalLen = obj.pktStat;
    }
    pthread_rwlock_unlock(&rwlock);

    return ERR_SGG_OK;
}

int sgg_audit_manage::rmv(Pkt5Tuple &key)
{
    int errCode = ERR_SGG_OK;
    std::map<Pkt5Tuple, sgg_audit_node>::iterator it;
    
    pthread_rwlock_wrlock(&rwlock);
    it = auditNodes.find(key);
    if(it != auditNodes.end()) {
        auditNodes.erase(it);
    } else {
        errCode = ERR_SGG_OBJ_NOT_EXIST;
    }
    pthread_rwlock_unlock(&rwlock);
    
    return errCode;
}

int sgg_audit_manage::rmv(Pkt3Tuple &key)
{
    int errCode = ERR_SGG_OK;
    std::map<Pkt5Tuple, sgg_audit_node>::iterator it;
    
    pthread_rwlock_wrlock(&rwlock);
    for(it=auditNodes.begin(); it!=auditNodes.end(); ) {
        if(it->second.pktInfo.dstIP==key.ip &&
            it->second.pktInfo.dstPort == key.port &&
            it->second.pktInfo.protocol == key.protocol) {
            it = auditNodes.erase(it);
        } else {
            it++;
        }
    }
    pthread_rwlock_unlock(&rwlock);
    
    return errCode;
}

/* 调用此接口后，如果出参len>0，需要调用函数释放ppbuf内存 */
int sgg_audit_manage::report(char**ppbuf, unsigned int &len)
{
    int errCode = ERR_SGG_OK;
    int totalSize = 0;
    int dataSize = 0;
    SggCommMsgInfo*pHead;
    sgg_audit_node *pNode;
    std::map<Pkt5Tuple, sgg_audit_node>::iterator it;

    DEBUG_PRINT("[%s][%d][%s]: audit report in...\r\n", __FILE__, __LINE__, __FUNCTION__);
    
    pthread_rwlock_rdlock(&rwlock);
    dataSize = auditNodes.size();
    dataSize *= sizeof(sgg_audit_node);
    totalSize = sizeof(SggCommMsgHead) + dataSize;
    *ppbuf = (char*)malloc(totalSize);
    if(*ppbuf == NULL) {
        pthread_rwlock_unlock(&rwlock);
        ELOG(L_ERROR,"allocate memory failed(%d)", totalSize);
        return ERR_SGG_ALLOCATE_MEMORY_FAILED;
    }
    pHead = (SggCommMsgInfo*)(*ppbuf);
    pHead->head.msgType = MSG_TYPE_RTP_AUDIT_RESP;
    pHead->head.version = 100;
    pHead->head.msgLen = totalSize;
    pHead->head.ackFlag = 0;
    pHead->head.errCode = ERR_SGG_OK;
    
    if(dataSize>0) {        
        pNode = (sgg_audit_node*)pHead->buf;        
        for(it=auditNodes.begin();it!=auditNodes.end();it++) {
            *pNode = it->second;
            pNode++;
        }
    }
    len = totalSize;
    pthread_rwlock_unlock(&rwlock);

    DEBUG_PRINT("[%s][%d][%s]: audit report out(totalSize=%u)...\r\n", __FILE__, __LINE__, __FUNCTION__, totalSize);
    ELOG(L_INFO ,"audit report out(totalSize=%u, errCode=%u)", totalSize, errCode);
    return errCode;
}
#define AuditManageInstance sgg_audit_manage::GetInstance()


sgg_rtp_encoding_manage* sgg_rtp_encoding_manage::GetInstance()
{
    static sgg_rtp_encoding_manage *instace = NULL;
    if(instace == NULL) {
        instace = new sgg_rtp_encoding_manage;
    }
    return instace;
}
sgg_rtp_encoding_manage::sgg_rtp_encoding_manage()
{
    rtpCodingNodes.clear();
    pthread_rwlock_init(&rwlock,NULL);
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
    plib = NULL;//指向SO文件的指针
    //打开so文件
    plib = dlopen("/usr/local/lib/libinspec.so", RTLD_NOW|RTLD_GLOBAL);
    if(NULL ==  plib) {
        FuncPackethandle = NULL;
        FuncDeleteChannelMediaInfo = NULL;
        FuncDeleteAllChannelMediaInfo = NULL;
        FuncGetChannelMediaInfo = NULL;
        ELOG(L_WARNING, "Failed to open the libinspec.so library file.:err_str=%s", strerror(errno));
    } else {
        FuncPackethandle = (pfunc_packethandle)dlsym(plib, "packethandle");
        FuncDeleteChannelMediaInfo = (pfunc_deleteChannelMediaInfo)dlsym(plib, "deleteChannelMediaInfo");
        FuncDeleteAllChannelMediaInfo = (pfunc_deleteAllChannelMediaInfo)dlsym(plib, "deleteAllChannelMediaInfo");
        FuncGetChannelMediaInfo = (pfunc_getChannelMediaInfo)dlsym(plib, "getChannelMediaInfo");
        if(FuncPackethandle == NULL ||
            FuncDeleteChannelMediaInfo == NULL ||
            FuncDeleteAllChannelMediaInfo == NULL ||
            FuncGetChannelMediaInfo == NULL){

            FuncPackethandle = NULL;
            FuncDeleteChannelMediaInfo = NULL;
            FuncDeleteAllChannelMediaInfo = NULL;
            FuncGetChannelMediaInfo = NULL;
            ELOG(L_WARNING, "Can`t load the encoding functions");
            //关闭so文件
            dlclose(plib);
        }
    }   
    
}

sgg_rtp_encoding_manage::~sgg_rtp_encoding_manage()
{
    for(auto it=rtpCodingNodes.begin(); it != rtpCodingNodes.end();it++) {
        it->second.clear();
    }
    rtpCodingNodes.clear();
    
    pthread_rwlock_destroy(&rwlock);
    DEBUG_PRINT("[%s][%d]:[%s]\r\n", __FILE__, __LINE__, __FUNCTION__);
    if(plib != NULL) {
        //关闭so文件
        dlclose(plib);
    }    
}

int sgg_rtp_encoding_manage::add(Pkt5Tuple &key, char*pktBuff, unsigned int pktLen)
{
    Pkt3Tuple tmp;
    RET_IF(FuncPackethandle== NULL, ERR_SGG_FUNCTION_NOT_SUPPORT);
    
    tmp.ip = key.dstIP;
    tmp.port = key.dstPort;
    tmp.protocol = key.protocol;
    
    pthread_rwlock_wrlock(&rwlock);    
    /* 重复选项通过set中只能插入不重复key值进行过滤 */
    auto it = rtpCodingNodes.find(tmp);
    if(it != rtpCodingNodes.end()) {
        it->second.insert(key);
    } else {
        std::set<Pkt5Tuple> stl_set;
        stl_set.clear();
        stl_set.insert(key);
        rtpCodingNodes.insert(std::make_pair(tmp,stl_set));
    }
    pthread_rwlock_unlock(&rwlock);

    // TODO: 调用接口进行解析    
    sgg_HashAddrInfo addr;
    addr._src_ip = key.srcIP;
    addr._src_port = (unsigned short)key.srcPort;
    addr._dst_ip = key.dstIP;
    addr._dst_port = (unsigned short)key.dstPort;
    addr._proto = key.protocol;
    //ELOG(L_TRACE, "FuncPackethandle: srcIP=0x%08x, srcPort=%u, dstIP=0x%08x, dstPort=%u, protocol=%u, pktLen=%u", key.srcIP, key.srcPort, key.dstIP, key.dstPort, key.protocol, pktLen)
    FuncPackethandle(pktBuff, pktLen, (void*)&addr);

    return ERR_SGG_OK;
}

int sgg_rtp_encoding_manage::rmv(Pkt5Tuple &key)
{
    int errCode = ERR_SGG_OK;
    sgg_HashAddrInfo addr;
    Pkt3Tuple tmp;
    
    RET_IF(FuncDeleteChannelMediaInfo== NULL, ERR_SGG_FUNCTION_NOT_SUPPORT);

    tmp.ip = key.dstIP;
    tmp.port = key.dstPort;
    tmp.protocol = key.protocol;
    
    pthread_rwlock_wrlock(&rwlock);
    auto it3 = rtpCodingNodes.find(tmp);
    if(it3 != rtpCodingNodes.end()) {
        auto it5 = it3->second.find(key);
        if(it5 != it3->second.end()){
            it3->second.erase(it5);
        } else {
            errCode = ERR_SGG_OBJ_NOT_EXIST;
            goto end;
        }
        if(it3->second.size()==0) {
            rtpCodingNodes.erase(it3);
        }
        // TODO: 调用删除接口        
        addr._src_ip = key.srcIP;
        addr._src_port = (unsigned short)key.srcPort;
        addr._dst_ip = key.dstIP;
        addr._dst_port = (unsigned short)key.dstPort;
        addr._proto = key.protocol;
        FuncDeleteChannelMediaInfo((void*)&addr);
    } else {
        errCode = ERR_SGG_OBJ_NOT_EXIST;
    }
    end:
    pthread_rwlock_unlock(&rwlock);
    
    return errCode;
}

int sgg_rtp_encoding_manage::rmv(Pkt3Tuple &key)
{
    int errCode = ERR_SGG_OK;   
    sgg_HashAddrInfo addr;
    RET_IF(FuncDeleteChannelMediaInfo== NULL, ERR_SGG_FUNCTION_NOT_SUPPORT);
    
    pthread_rwlock_wrlock(&rwlock);  

    auto it3 = rtpCodingNodes.find(key);
    if(it3 != rtpCodingNodes.end()) {
        for(auto it5=it3->second.begin(); it5!= it3->second.end(); it5++) {
            addr._src_ip = it5->srcIP;
            addr._src_port = (unsigned short)it5->srcPort;
            addr._dst_ip = it5->dstIP;
            addr._dst_port = (unsigned short)it5->dstPort;
            addr._proto = it5->protocol;
            FuncDeleteChannelMediaInfo((void*)&addr);
        }
        rtpCodingNodes.erase(it3);
    } else {
        errCode = ERR_SGG_OBJ_NOT_EXIST;
    }    
    pthread_rwlock_unlock(&rwlock);
    
    return errCode;
}


/* 调用此接口后，如果出参len>0，需要调用函数释放ppbuf内存 */
int sgg_rtp_encoding_manage::getcoding(const char*reqData, unsigned int reqLen, char**ppbuf, unsigned int &len)
{
    int errCode = ERR_SGG_OK;
    int totalSize = 0;
    int dataSize = 0;
    SggCommMsgInfo*pHead;
    sgg_rtp_encoding_node *pNode;
    sgg_MultiMediaInfo result;
    sgg_HashAddrInfo addr;
    bool ret = false;
    unsigned int relCnt = 0;
    Pkt3Tuple *ptr = (Pkt3Tuple*)reqData;
    unsigned int tmpLen = 0;
    //std::map<Pkt5Tuple, sgg_rtp_encoding_node>::iterator it;
    RET_IF(FuncGetChannelMediaInfo== NULL, ERR_SGG_FUNCTION_NOT_SUPPORT);
    
    
    ELOG(L_DEBUG, "recv getting encode format msg...");    
    pthread_rwlock_rdlock(&rwlock);
    /* 查询所有 */
    if(ptr->ip == 0xffffffff) {
        for(auto it3=rtpCodingNodes.begin(); it3 != rtpCodingNodes.end();it3++) {
            dataSize+=it3->second.size();
        }
        dataSize *= sizeof(sgg_rtp_encoding_node);
        totalSize = sizeof(SggCommMsgHead) + dataSize;
        *ppbuf = (char*)malloc(totalSize);
        if(*ppbuf == NULL) {
            pthread_rwlock_unlock(&rwlock);
            ELOG(L_ERROR,"allocate memory failed(%d)", totalSize);
            return ERR_SGG_ALLOCATE_MEMORY_FAILED;
        }
        pHead = (SggCommMsgInfo*)(*ppbuf);
        pHead->head.msgType = MSG_TYPE_RTP_CODING_GET_RESP;
        pHead->head.version = 100;
        pHead->head.ackFlag = 0;
        pHead->head.errCode = ERR_SGG_OK;  
        pNode = (sgg_rtp_encoding_node*)pHead->buf;        
    
        for(auto it3=rtpCodingNodes.begin(); it3!=rtpCodingNodes.end();it3++) {
            for(auto it5=it3->second.begin(); it5!=it3->second.end(); it5++) {
                addr._src_ip = it5->srcIP;
                addr._src_port = (unsigned short)it5->srcPort;
                addr._dst_ip = it5->dstIP;
                addr._dst_port = (unsigned short)it5->dstPort;
                addr._proto = it5->protocol;
                ret = FuncGetChannelMediaInfo((void*)&addr, (void*)&result);
                /*result._result = 1;
                result._format =1;
                result._audia_type = 2;
                result._video_type = 3;
                ret = true;*/
                if(ret==true && result._result==1) {
                    pNode->enCapsulationFormat = result._format;
                    pNode->audio_type = result._audia_type;
                    pNode->video_type = result._video_type;
                    pNode->pktInfo = *it5;
                    pNode++;
                    relCnt++;
                    ELOG(L_INFO, "Successful to get media message stream encoding format: srcIP=0x%08x, srcPort=%u, dstIP=0x%08x, dstPort=%u, protocol=%u, format=%u, audio_type=%u,video_type=%u", 
                        it5->srcIP, it5->srcPort, it5->dstIP, it5->dstPort, it5->protocol, result._format, result._audia_type, result._video_type);
                    
                } else {
                    ELOG(L_WARNING, "Failed to get media message stream encoding format:ret=%u, res=%u, srcIP=0x%08x, srcPort=%u, dstIP=0x%08x, dstPort=%u, protocol=%u", 
                        ret, result._result, it5->srcIP, it5->srcPort, it5->dstIP, it5->dstPort, it5->protocol);
                }
            }
        }
        pHead->head.msgLen = sizeof(SggCommMsgHead) + relCnt*sizeof(sgg_rtp_encoding_node);
        
    } else {
        /* 查询指定IP地址 */
        do{
            auto it3 = rtpCodingNodes.find(*ptr);
            if(it3 != rtpCodingNodes.end()) {
                dataSize+=it3->second.size();
            }
            ptr++;
            tmpLen+=sizeof(Pkt3Tuple);
        }while(tmpLen<reqLen);
        
        dataSize *= sizeof(sgg_rtp_encoding_node);
        totalSize = sizeof(SggCommMsgHead) + dataSize;
        *ppbuf = (char*)malloc(totalSize);
        if(*ppbuf == NULL) {
            pthread_rwlock_unlock(&rwlock);
            ELOG(L_ERROR,"allocate memory failed(%d)", totalSize);
            return ERR_SGG_ALLOCATE_MEMORY_FAILED;
        }
        pHead = (SggCommMsgInfo*)(*ppbuf);
        pHead->head.msgType = MSG_TYPE_RTP_CODING_GET_RESP;
        pHead->head.version = 100;
        pHead->head.ackFlag = 0;
        pHead->head.errCode = ERR_SGG_OK;  
        pNode = (sgg_rtp_encoding_node*)pHead->buf; 

        ptr = (Pkt3Tuple*)reqData;
        tmpLen = 0;
        do{
            auto it3 = rtpCodingNodes.find(*ptr);
            if(it3 != rtpCodingNodes.end()) {
                for(auto it5=it3->second.begin(); it5!=it3->second.end(); it5++) {
                    addr._src_ip = it5->srcIP;
                    addr._src_port = (unsigned short)it5->srcPort;
                    addr._dst_ip = it5->dstIP;
                    addr._dst_port = (unsigned short)it5->dstPort;
                    addr._proto = it5->protocol;
                    ret = FuncGetChannelMediaInfo((void*)&addr, (void*)&result);
                    /*result._result = 1;
                    result._format =1;
                    result._audia_type = 2;
                    result._video_type = 3;
                    ret = true;*/
                    if(ret==true && result._result==1) {
                        pNode->enCapsulationFormat = result._format;
                        pNode->audio_type = result._audia_type;
                        pNode->video_type = result._video_type;
                        pNode->pktInfo = *it5;
                        pNode++;
                        relCnt++;
                        ELOG(L_INFO, "Successful to get media message stream encoding format: srcIP=0x%08x, srcPort=%u, dstIP=0x%08x, dstPort=%u, protocol=%u, format=%u, audio_type=%u,video_type=%u", 
                            it5->srcIP, it5->srcPort, it5->dstIP, it5->dstPort, it5->protocol, result._format, result._audia_type, result._video_type);
                    } else {
                        ELOG(L_WARNING, "Failed to get media message stream encoding format:ret=%u, res=%u, srcIP=0x%08x, srcPort=%u, dstIP=0x%08x, dstPort=%u, protocol=%u", 
                            ret, result._result, it5->srcIP, it5->srcPort, it5->dstIP, it5->dstPort, it5->protocol);
                    }
                }
            }
            ptr++;
            tmpLen+=sizeof(Pkt3Tuple);
        }while(tmpLen<reqLen);
    
        pHead->head.msgLen = sizeof(SggCommMsgHead) + relCnt*sizeof(sgg_rtp_encoding_node);        
    }
    
    len = pHead->head.msgLen;
    pthread_rwlock_unlock(&rwlock);

    ELOG(L_DEBUG,"get encoding format proc out(totalSize=%u)...", totalSize);
    ELOG(L_INFO ,"get encoding format proc out(totalSize=%u, errCode=%u)", totalSize, errCode);
    return errCode;
}

void sgg_rtp_encoding_manage::Packethandle(const char *buf, unsigned int len, void *addr)
{
    pfunc_packethandle FuncPackethandle = NULL;    
    void *plib = NULL;//指向SO文件的指针
    //打开so文件
    plib = dlopen("/usr/local/lib/libinspec.so", RTLD_NOW|RTLD_GLOBAL);
    if(NULL ==  plib) {
        ELOG(L_WARNING, "Can`t open the libinspec.so");
    } else {
        FuncPackethandle = (pfunc_packethandle)dlsym(plib, "packethandle");
        if(FuncPackethandle != NULL) {
            FuncPackethandle(buf,len, addr);
        }
    }
    //关闭so文件
    dlclose(plib);
}

void sgg_rtp_encoding_manage::DeleteChannelMediaInfo(void *addr)
{
    pfunc_deleteChannelMediaInfo FuncDeleteChannelMediaInfo = NULL;
    void *plib = NULL;//指向SO文件的指针
    //打开so文件
    plib = dlopen("/usr/local/lib/libinspec.so", RTLD_NOW|RTLD_GLOBAL);
    if(NULL ==  plib) {
        ELOG(L_WARNING, "Can`t open the libinspec.so");
    } else {
        FuncDeleteChannelMediaInfo = (pfunc_deleteChannelMediaInfo)dlsym(plib, "deleteChannelMediaInfo");
        if(FuncDeleteChannelMediaInfo != NULL) {
            FuncDeleteChannelMediaInfo(addr);
        }
    }
    //关闭so文件
    dlclose(plib);
}

void sgg_rtp_encoding_manage::DeleteAllChannelMediaInfo(void)
{
    pfunc_deleteAllChannelMediaInfo FuncDeleteAllChannelMediaInfo = NULL;    
    void *plib = NULL;//指向SO文件的指针
    //打开so文件
    plib = dlopen("/usr/local/lib/libinspec.so", RTLD_NOW|RTLD_GLOBAL);
    if(NULL ==  plib) {
        ELOG(L_WARNING, "Can`t open the libinspec.so");
    } else {
        FuncDeleteAllChannelMediaInfo = (pfunc_deleteAllChannelMediaInfo)dlsym(plib, "deleteAllChannelMediaInfo");
        if(FuncDeleteAllChannelMediaInfo != NULL){
            FuncDeleteAllChannelMediaInfo();
        }
    }
    //关闭so文件
    dlclose(plib);
}

bool sgg_rtp_encoding_manage::ChannelMediaInfo(void *addr, void *media)
{
    pfunc_getChannelMediaInfo FuncGetChannelMediaInfo = NULL;
    void *plib = NULL;//指向SO文件的指针
    bool ret = false;
    //打开so文件
    plib = dlopen("/usr/local/lib/libinspec.so", RTLD_NOW|RTLD_GLOBAL);
    if(NULL ==  plib) {
        ELOG(L_WARNING, "Can`t open the libinspec.so");
    } else {
        FuncGetChannelMediaInfo = (pfunc_getChannelMediaInfo)dlsym(plib, "getChannelMediaInfo");
        if(FuncGetChannelMediaInfo != NULL) {
            ret = FuncGetChannelMediaInfo(addr, media);
        }
    }
    //关闭so文件
    dlclose(plib);
    return ret;
}


#define RtpCodingManageInstance sgg_rtp_encoding_manage::GetInstance()


static int sgg_GetRtpAuditMsgProc(void*inData, int inLen, void*outData, int* outLen) 
{
    SggCommMsgInfo*pInMsg = (SggCommMsgInfo*)inData;
    SggCommMsgInfo*pOutMsg = (SggCommMsgInfo*)outData;
    unsigned int errCode = ERR_SGG_OK;
    char *pbuf = NULL;
    unsigned int bufLen = 0;


    if((pInMsg->head.msgLen < sizeof(SggCommMsgInfo)) || (pInMsg->head.msgLen != (unsigned int)inLen)) {
        errCode = ERR_SGG_INVALID_MSG_BODY;
        ELOG(L_ERROR,"rtp audit msg len is invalid: msglen=%d", pInMsg->head.msgLen);
        goto end;
    }
    
    ELOG(L_INFO,"recved a rtp audit msg: msgType=0x%08x, msgLen=%d, ackFlag=%u,version=%u,inLen=%d", 
        pInMsg->head.msgType,pInMsg->head.msgLen, pInMsg->head.ackFlag, pInMsg->head.version,inLen);
    AuditManageInstance->report(&pbuf, bufLen);
    if(*outLen >= (int)bufLen) {
        memcpy(outData, pbuf, bufLen);
        free(pbuf);        
    } else {
        size_t aa = (size_t)&pbuf;
	    memcpy(outData, &aa, sizeof(size_t));        
    }
    *outLen = bufLen;
        
    return ERR_SGG_OK;

end:
    if(pInMsg->head.ackFlag == 0) {
        *outLen = 0;
    } else {
        *outLen = sizeof(SggCommMsgHead);
        pOutMsg->head.msgType = MSG_TYPE_RTP_AUDIT_RESP;
        pOutMsg->head.msgLen = sizeof(SggCommMsgHead);
        pOutMsg->head.version = 100;
        pOutMsg->head.errCode = errCode;
        pOutMsg->head.ackFlag = 0;
    }
    
    return errCode;
}

static int sgg_GetRtpCodingMsgProc(void*inData, int inLen, void*outData, int* outLen) 
{
    SggCommMsgInfo*pInMsg = (SggCommMsgInfo*)inData;
    SggCommMsgInfo*pOutMsg = (SggCommMsgInfo*)outData;
    unsigned int errCode = ERR_SGG_OK;
    char *pbuf = NULL;
    unsigned int bufLen = 0;


    if((pInMsg->head.msgLen <= sizeof(SggCommMsgInfo)) || (pInMsg->head.msgLen != (unsigned int)inLen)) {
        errCode = ERR_SGG_INVALID_MSG_BODY;
        ELOG(L_ERROR,"rtp audit msg len is invalid: msglen=%d", pInMsg->head.msgLen);
        goto end;
    }
    
    ELOG(L_INFO,"recved a rtp encode getting msg: msgType=0x%08x, msgLen=%d, ackFlag=%u,version=%u,inLen=%d", 
        pInMsg->head.msgType,pInMsg->head.msgLen, pInMsg->head.ackFlag, pInMsg->head.version,inLen);
    errCode = RtpCodingManageInstance->getcoding(pInMsg->buf, (inLen-sizeof(SggCommMsgHead)),&pbuf, bufLen);
    if(errCode != ERR_SGG_OK) {
        if(pbuf != NULL) {
            free(pbuf);
        }
        goto end;
    }
    if(*outLen >= (int)bufLen) {
        memcpy(outData, pbuf, bufLen);
        free(pbuf);        
    } else {
        size_t aa = (size_t)&pbuf;
        memcpy(outData, &aa, sizeof(size_t));
    }
    *outLen = bufLen;
        
    return ERR_SGG_OK;

end:
    if(pInMsg->head.ackFlag == 0) {
        *outLen = 0;
    } else {
        *outLen = sizeof(SggCommMsgHead);
        pOutMsg->head.msgType = MSG_TYPE_RTP_CODING_GET_RESP;
        pOutMsg->head.msgLen = sizeof(SggCommMsgHead);
        pOutMsg->head.version = 100;
        pOutMsg->head.errCode = errCode;
        pOutMsg->head.ackFlag = 0;
    }
    
    return errCode;
}


int sgg_auditInit()
{
    comm_registerMsg(MSG_TYPE_RTP_AUDIT_REQ, sgg_GetRtpAuditMsgProc);
    comm_registerMsg(MSG_TYPE_RTP_CODING_GET_REQ, sgg_GetRtpCodingMsgProc);
    return ERR_SGG_OK;
}

int sgg_RtpAuditAdd(Pkt5Tuple &key, char*pktBuff, unsigned int pktLen, char rtpAuditFlag[4])
{
    sgg_audit_node obj;
    int interval = 0; /* 插白帧间隔 */
    int len = 0; /* 白帧长度 */
    int nullLen = 0;
    unsigned long long totalLen = 0;
    unsigned long long start = 0;
    int offset;
    char *ptr = pktBuff;
    DEBUG_PRINT("[%s][%d][%s]: rtpAuditFlag=%u-%u-%u-%u\n", __FILE__, __LINE__, __FUNCTION__, 
        (int)rtpAuditFlag[0], (int)rtpAuditFlag[1],(int)rtpAuditFlag[2], (int)rtpAuditFlag[3]);
    
    /* RTP流审计信息上报\RTP流插白帧 */
    if(rtpAuditFlag[0] != 0 || rtpAuditFlag[1] == 1) {

        if(SL_BITGET(rtpAuditFlag[0], BIT_MASK_AUDIT) != 0) {
            obj.pktInfo = key;
            obj.pktStat = pktLen;
            if(sizeof(AuditDetail)<=pktLen) {            
                memcpy(obj.detailInfo.rtpInfo, pktBuff, sizeof(AuditDetail));
            }
            AuditManageInstance->add(key, obj, totalLen);
        }
        
        if(SL_BITGET(rtpAuditFlag[0], BIT_MASK_CODING) != 0) {
            RtpCodingManageInstance->add(key, pktBuff, pktLen);
        }

        /* 插白帧 */
        if ((rtpAuditFlag[1] == 1)&&(rtpAuditFlag[2]!=0)&&(rtpAuditFlag[3]!=0)) {
    		len = (int)rtpAuditFlag[3];
    		interval = ((int)rtpAuditFlag[2]) * 1024 - len;
    		start = totalLen - pktLen;
    		offset = start % (interval + len);
    		start += (interval - offset);
    		offset = start % (interval + len);
    		if (offset >= interval) {
    			offset = start-(totalLen - pktLen);
    		}
    		
    		while (start < totalLen) {
    			if (start + len>totalLen) {
    				nullLen = totalLen - start;
    			} else {
    				nullLen = len;				
    			}
    			if (offset < 0) {
    				nullLen += offset;
    				offset = 0;
    			}
    			bzero(ptr + offset, nullLen);
    			start += (interval + len);
    			offset += (interval + nullLen);
    		};
    	}
    }

    return ERR_SGG_OK;
}

int sgg_RtpAuditRmv(Pkt5Tuple &key)
{
    AuditManageInstance->rmv(key);
    RtpCodingManageInstance->rmv(key);
    return ERR_SGG_OK;
}
int sgg_RtpAuditRmv(Pkt3Tuple &key)
{
    AuditManageInstance->rmv(key);
    RtpCodingManageInstance->rmv(key);
    return ERR_SGG_OK;
}


