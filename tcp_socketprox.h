#ifndef __TCP_SOCKET_PROX_H__
#define __TCP_SOCKET_PROX_H__

#define TCP_PROX_PROTOCAL_BUFF_MAX_SIZE (64*1024)
#define TCP_PROX_SEND_TASK_MAX_NUM         6000
#define TCP_PROX_SEND_TASK_MIN_NUM         3000
#define TCP_PROX_RECV_TASK_MAX_NUM         6000

int runForwardProxyServer(void*arg);
int runReverseProxyServer(void*arg);

#endif
