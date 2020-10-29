#ifndef __SGG_AUDIT_H__
#define __SGG_AUDIT_H__

#include "sgg_manage.h"
#include <map>
#include <set>
#include <pthread.h>


#define TIME_STR_LEN    20
#define RTP_DETAIL_LEN  12
typedef struct {
   char rtpInfo[RTP_DETAIL_LEN]; 
}AuditDetail;

enum {
    BIT_MASK_AUDIT = 0,
    BIT_MASK_CODING = 1,
    BIT_MASK_BUTT,
};


typedef struct 
{
	unsigned int    _src_ip;
	unsigned int    _dst_ip;
	unsigned short  _src_port;
	unsigned short  _dst_port;
	unsigned int    _proto;
}sgg_HashAddrInfo;

typedef struct 
{
	unsigned int _result;//探测结果	
	unsigned int _format;
	unsigned int _audia_type;
	unsigned int _video_type;
	unsigned int reserve[32];
}sgg_MultiMediaInfo;



/*
*callback function: put packet to which address
* @param buf  -- packet data
* @param len  -- packet data length
* @param addr --five tuple with source ip port、Destination ip port、 protocol(udp, tcp)
*/
typedef void (*pfunc_packethandle)(const char *buf, unsigned int len, void *addr);

/*
*delete library storage info about one address to check media info
* @param addr --five tuple with source ip port、Destination ip port、 protocol(udp, tcp)
*/
typedef void (*pfunc_deleteChannelMediaInfo)(void *addr);

/*
*delete library storage info about all addresses to check media info
* @param addr --five tuple with source ip port、Destination ip port、 protocol(udp, tcp)
*/
typedef void (*pfunc_deleteAllChannelMediaInfo)();

/*
*get  check media info by address 
*@param addr --five tuple with source ip port、Destination ip port、 protocol(udp, tcp)
*@return  check failed or success
*/
typedef bool (*pfunc_getChannelMediaInfo)(void *addr, void *media);


#pragma pack(push)
#pragma pack(4)
class sgg_audit_node
{
    public:
        sgg_audit_node();
        sgg_audit_node(const sgg_audit_node& obj);
        ~sgg_audit_node();
        inline bool operator == (const sgg_audit_node &obj) const;
        sgg_audit_node& operator= (const sgg_audit_node& obj);

    public:
        Pkt5Tuple pktInfo;
        unsigned long long pktStat;
        char startTime[TIME_STR_LEN];
        char endTime[TIME_STR_LEN];
        AuditDetail detailInfo;
        
};
#pragma pack(pop)



class sgg_audit_manage 
{
    public:
        sgg_audit_manage();
        ~sgg_audit_manage();
        static sgg_audit_manage* GetInstance();
        int add(Pkt5Tuple &key, sgg_audit_node&obj, unsigned long long &totalLen);
        int rmv(Pkt5Tuple &key);
        int rmv(Pkt3Tuple &key);
        //int update(Pkt5Tuple &key, sgg_audit_node&obj);
        int report(char**ppbuf, unsigned int &len);
    private:
        std::map<Pkt5Tuple, sgg_audit_node> auditNodes;
        pthread_rwlock_t rwlock;
};


class sgg_rtp_encoding_node {

public:
    sgg_rtp_encoding_node();
    sgg_rtp_encoding_node(const sgg_rtp_encoding_node& obj);
    ~sgg_rtp_encoding_node();
    inline bool operator == (const sgg_rtp_encoding_node &obj) const;
    sgg_rtp_encoding_node& operator= (const sgg_rtp_encoding_node& obj);

public:
    Pkt5Tuple pktInfo;
    unsigned int enCapsulationFormat;
    unsigned int audio_type;
    unsigned int video_type;
};

typedef struct{
    unsigned int ip;
    std::map<unsigned int, std::set<Pkt5Tuple>> node;
}sgg_service;

class sgg_rtp_encoding_manage {
public:
    sgg_rtp_encoding_manage();
    ~sgg_rtp_encoding_manage();
    static sgg_rtp_encoding_manage* GetInstance();
    int add(Pkt5Tuple &key,  char*pktBuff, unsigned int pktLen);
    int rmv(Pkt5Tuple &key);
    int rmv(Pkt3Tuple &key);
    int getcoding(const char*reqData, unsigned int reqLen, char**ppbuf, unsigned int &len);
    static void Packethandle(const char *buf, unsigned int len, void *addr);
    static void DeleteChannelMediaInfo(void *addr);
    static void DeleteAllChannelMediaInfo(void);
    static bool ChannelMediaInfo(void *addr, void *media);
private:
    pthread_rwlock_t rwlock;
    //std::map<Pkt5Tuple, sgg_rtp_encoding_node> rtpCodingNodes;
    std::set<Pkt5Tuple> rtpCodingNodess;

    std::map<Pkt3Tuple, std::set<Pkt5Tuple>> rtpCodingNodes;

    void *plib = NULL;//指向SO文件的指针
    pfunc_packethandle FuncPackethandle;
    pfunc_deleteChannelMediaInfo FuncDeleteChannelMediaInfo;
    pfunc_deleteAllChannelMediaInfo FuncDeleteAllChannelMediaInfo;
    pfunc_getChannelMediaInfo FuncGetChannelMediaInfo;
    
};


int sgg_auditInit();

/* 覆盖式添加，不存在则添加，存在则更新结束时间和报文包数统计 */
int sgg_RtpAuditAdd(Pkt5Tuple &key, char*pktBuff, unsigned int pktLen, char rtpAuditFlag[4]);

/* 根据五元组信息进行删除 */
int sgg_RtpAuditRmv(Pkt5Tuple &key);

/* 根据目标端三元组信息进行删除 */
int sgg_RtpAuditRmv(Pkt3Tuple &key);



#endif

