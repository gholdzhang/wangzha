#ifndef __SGG_COMM_H__
#define __SGG_COMM_H__
#include "sgg_manage.h"

typedef int (*fun_SggMsgProc)(void*inData, int inLen, void*outData, int* outLen);
typedef enum _SGG_MSG_TYPE{
    MSG_TYPE_END = 0x42010203,
    MSG_TYPE_ADD_SERVER_REQ = 0x420A0000,
    MSG_TYPE_ADD_SERVER_RESP = 0x420A0001,
    MSG_TYPE_RMV_SERVER_REQ = 0x420A0002,
    MSG_TYPE_RMV_SERVER_RESP = 0x420A0003,
    MSG_TYPE_RTP_AUDIT_REQ = 0x420A0004,
    MSG_TYPE_RTP_AUDIT_RESP = 0x420A0005,
    MSG_TYPE_RTP_CODING_GET_REQ = 0x420A0006,
    MSG_TYPE_RTP_CODING_GET_RESP = 0x420A0007,
    
    MSG_TYPE_GET_SERVER_FLOW_STAT_REQ = 0x420B0004,
    MSG_TYPE_GET_SERVER_FLOW_STAT_RESP = 0x420B0005,
    
    MSG_TYPE_BUTT
}SGG_MSG_TYPE;

typedef struct _SggCommMsgHead{
    unsigned int msgType;       /* 消息类型ID */
    unsigned int msgLen;        /* 消息总长度，包括消息头部长度 */
    unsigned int ackFlag;       /* 针对主动发送的消息是否需要对端回response响应报文，如果需要则填写1，对端发送消息时msgType=本消息发出的消息msgType+1 */
    unsigned int version;       /* 消息体版本号，扩展使用，当前固定填写100 */
    unsigned int errCode;       /* 错误码，成功填写0 */
    unsigned int reverse;       /* 保留字段，当前不适用*/
}SggCommMsgHead;

typedef struct {
    SggCommMsgHead head;        /* 消息头 */
    char buf[0];                /* 消息体,消息 */
}SggCommMsgInfo;


class serverCommObj {
    public:
        serverCommObj(){}
        serverCommObj(unsigned int ip, unsigned int port, unsigned protocol);
        ~serverCommObj();
        int run(void);
    private:
        Pkt3Tuple socketCfg;
};
void RunCommServer(Pkt3Tuple &serverInfo);
int comm_registerMsg(unsigned int msgId, fun_SggMsgProc pfun);
#endif