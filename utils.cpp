#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctype.h>
#include "utils.h"


int file_exist_check(char* file) {
    struct stat st;
    if(access(file, F_OK) != 0) {     //文件不存在  
        printf("sgg_app run failed: The configuration file does not exist or is illegal.\n");
        syslog(LOG_ERR, "%s", "sgg_app run failed: The configuration file does not exist or is illegal.\n");
        return -1;
    }
    stat(file,&st);
    if (S_ISDIR(st.st_mode)) {    //非文件
        printf("sgg_app run failed: The configuration file does not exist or is illegal.\n");
        syslog(LOG_ERR, "%s", "sgg_app run failed: The configuration file does not exist or is illegal.\n");
        return -1;
    }
    return 0;

}

#define MSG_COMM_MAX_LEN 100
struct msgbuf_u 
{
    long mtype;     //消息类型
    char mtext[MSG_COMM_MAX_LEN];  //消息内容
};

int createmsgcomm(key_t msgkey) {
    int msgid;
    msgid = msgget(msgkey,IPC_CREAT/* |IPC_EXCL*/ | 0666); //打开或创建消息队列
    if(-1 == msgid) {
        perror("msgid");
        exit(1);
    }
    return msgid;
}

void deletemsgcomm(int msgid) {
    if(msgid > 0) {
        msgctl(msgid,IPC_RMID,NULL);//移除消息队列
    }
    return;
}

int recivemsgcomm(key_t msgkey, int tid, char *data, int data_len)
{
    struct msgbuf_u buf;
    int ret = 0;
    int msgid = 0;

    if(data_len < MSG_COMM_MAX_LEN) {
        return -1;
    }
    
    msgid = msgget(msgkey,0); //创建消息队列,返回标识符
    if(-1 == msgid) {
        perror("msgid");
        return -1;
    }
    memset(&buf,0,sizeof(buf));  //初始化
    ret = msgrcv(msgid, &buf,sizeof(buf.mtext),tid,IPC_NOWAIT);
    
    memcpy(data, buf.mtext, MSG_COMM_MAX_LEN);
    
    return ret;
}


int sendmsgcomm(key_t msgkey, int tid, char *data, int data_len)
{
    struct msgbuf_u buf;
    int ret = 0;
    int msgid = 0;

    if(data_len > MSG_COMM_MAX_LEN) {
        return -1;
    }
    msgid = msgget(msgkey,0); //创建消息队列,返回标识符
    if(-1 == msgid) {
        perror("msgid");
        return -1;
    }
    buf.mtype = tid;
    memcpy(buf.mtext, data, data_len);
    ret = msgsnd(msgid ,&buf,sizeof(buf.mtext),IPC_NOWAIT);
    return ret;
}


unsigned int htoi(char s[])
{
    int i;
    unsigned int n = 0;
    if (s[0] == '0' && (s[1]=='x' || s[1]=='X')){
        i = 2;
    } else {
        i = 0;
    }
    for (; (s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >='A' && s[i] <= 'Z');++i)
    {
        if (tolower(s[i]) > '9'){
            n = 16 * n + (10 + tolower(s[i]) - 'a');
        } else {
            n = 16 * n + (tolower(s[i]) - '0');
        }
    }
    return n;
}

void HexCodePrint(void* data, int len)
{
    int i = 0;
    unsigned char* pdata = (unsigned char*)data;
    printf("\r\ndataLen=%08d\r\n", len);
    for(; i < len; i++) {
        printf("%02x ", (unsigned int)pdata[i]);
    }
    printf("\r\n");
}

int addTimer(THandle*handle, unsigned long interval){

    if(*handle != NULL) {
        return 0;
    }
    *handle = (THandle)malloc(sizeof(_TimerStr));
    if(*handle == NULL) {
        return -1;    
    } else {
        (void)gettimeofday(&((*handle)->tv), &((*handle)->tz));
        (*handle)->interval = interval;
        (*handle)->isTimeout = false;
        return 0;
    }    
}

bool isTimerOut(THandle handle){
    struct timeval tv;
    struct timezone tz;  
    unsigned long interval = 0;
    if(handle == NULL) {
        return false;
    }
    if(handle->isTimeout) {
        return handle->isTimeout;
    }
    (void)gettimeofday(&tv, &tz);

    
    interval = ((unsigned long)tv.tv_sec - ((unsigned long)handle->tv.tv_sec))*1000000 + tv.tv_usec - handle->tv.tv_usec;
    
    if(interval>handle->interval){
        handle->isTimeout = true;
    }
    return handle->isTimeout;
}

void delTimer(THandle*handle){
    if(*handle != NULL) {
        free(*handle);
        *handle = NULL;
    }    
    return;
    
}
