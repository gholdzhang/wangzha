#include <stdio.h>  
#include <stdlib.h>  
#include <time.h>  
#include <unistd.h>  
#include <assert.h>  
#include <string.h>  
#include <string>  
#include <fcntl.h>  
#include <stdarg.h> 
#include "easylogging++.h"
#include "log.h"
  
char log_file_prefix[512] = {0};
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOG_FILE_DEFAULT_DIR "./log/"

int log_init(char *prefix) {
    assert(prefix != NULL);
    strncpy(log_file_prefix, prefix, 512);
    //elog_init(prefix);
    //elog_write(0, L_INFO, "hello sgg app:%u", 11111);
    return 0;
}

inline int get_logfilepath(char *filepath, int len) {
    int cnt = readlink("/proc/self/exe", filepath, len);
	if (cnt < 0 || cnt >= len)
	{
		printf("***get_logfilepath Error***\n");
		return -1;
	}
	//获取当前目录绝对路径，即去掉程序名
	char *p = strrchr(filepath, '/');
    *(p+1) = '\0';
    return p+1-filepath;
}

int log_file(unsigned int mode, char*file_path, int len)  
{  
    char filetime[32] = {0};  
    struct tm tm_time;  
    time_t t_log;
    int ret = -1;
    std::string log_time = ""; 

    memset(file_path, 0, len);
  
    //assert(getcwd(file_path, 512) != NULL);    //当前目录  
    ret = get_logfilepath(file_path, len);
    if ((ret > 0) && (file_path[strlen(file_path) - 1] != '/')) {  
        file_path[strlen(file_path)] = '/';  
    } 
    strcat(file_path, LOG_FILE_DEFAULT_DIR);
    
    pthread_mutex_lock(&(log_mutex));
    if(access(file_path, F_OK) != 0) {     //目录不存在  
        std::string build_path ="mkdir -p ";  
        build_path += file_path;
        if(system(build_path.c_str())==-1) {
            pthread_mutex_unlock(&(log_mutex));
            return -1;
        }
    }
    pthread_mutex_unlock(&(log_mutex));
  
    t_log = time(NULL);  
    localtime_r(&t_log, &tm_time);  
    strftime(filetime, sizeof(filetime), "%Y%m%d%H%M%S", &tm_time); //日志的时间  
    switch(mode) {  //日志存储模式  
    case mode_minute:  
        log_time.assign(filetime, 0, 12);  
        break;  
    case mode_hour:  
        log_time.assign(filetime, 0, 10);  
        break;  
    case mode_day:  
        log_time.assign(filetime, 0, 8);  
        break;  
    case mode_month:  
        log_time.assign(filetime, 0, 6);  
        break;  
    default:  
        log_time.assign(filetime, 0, 8);  
    }  
    strcat(file_path, "sgg_app_log_");
    if(strlen(log_file_prefix) != 0) {
        strcat(file_path, log_file_prefix);  
    }
    strcat(file_path, log_time.c_str());  
    strcat(file_path, ".log");  
    
    return 0;  
}  
void write_cmd(const char *fmt,...)  
{  
#ifndef _DEBUG_OFF_
    va_list ap;  
    va_start(ap,fmt);  
    vprintf(fmt,ap);  
    va_end(ap);  
#endif
    return;
}  
int write_log(const unsigned int mode, const char *msg, ...)  
{  
    int file_fd = -1;
    char file_path[512] = {0};
    int len = 512;
    char final[2048] = {0};   //当前时间记录  
    va_list vl_list;  
    va_start(vl_list, msg);  
    char content[1024] = {0};  
    vsprintf(content, msg, vl_list);   //格式化处理msg到字符串  
    va_end(vl_list);  
  
    time_t  time_write;  
    struct tm tm_Log;  
    if (msg == NULL) {        
        return -1;
    }
    log_file(mode, file_path, len);
    
    time_write = time(NULL);        //日志存储时间  
    localtime_r(&time_write, &tm_Log);  
    strftime(final, sizeof(final), "[%Y-%m-%d %H:%M:%S] ", &tm_Log);    
    strncat(final, content, strlen(content));    

    pthread_mutex_lock(&(log_mutex));
    file_fd = open(file_path, O_RDWR|O_CREAT|O_APPEND, 0666);  
    if ((msg != NULL && file_fd == -1)) {
        pthread_mutex_unlock(&(log_mutex));
        return -1;
    }
    if(write(file_fd, final, strlen(final)) != (int)strlen(final)) {
        pthread_mutex_unlock(&(log_mutex));
        close(file_fd);
        file_fd = -1;
        return -1;
    }
    close(file_fd);
    file_fd = -1;
    pthread_mutex_unlock(&(log_mutex));
    return 0;  
}  
void close_file()  
{  
    return;
    
}  

INITIALIZE_EASYLOGGINGPP
//多线程添加编译宏 -D ELPP_THREAD_SAFE

#define LOG_CONF_FILE_NAME "sailing_log.conf"


static void rolloutHandler(const char* filename, std::size_t size)   
{  
    time_t t_log;
    char filetime[32] = {0};  
    char filePath[512] = {0};
    struct tm tm_time;
    static int index = 0;
    t_log = time(NULL);  
    localtime_r(&t_log, &tm_time);  
    strftime(filetime, sizeof(filetime), "%Y%m%d%H%M%S", &tm_time); //日志的时间  
      
    if(access("log", F_OK) != 0) {
        /// 备份日志  
        system("mkdir log");
    } 
    
    std::stringstream ss;
    strncpy(filePath, filename, 512);
    char *p = strrchr(filePath, '/');
    if(p==NULL){    
        ss << "mv " << filename << " ./log/" << filetime << "_" << ++index <<  "_"<<filename;  
    } else {
        ss << "mv " << filename << " ./log/" << filetime << "_"<< ++index << "_"<< (p+1);
    }
    system(ss.str().c_str());  
} 

int elog_init(char*logFileFullPath)
{
    if (access(LOG_CONF_FILE_NAME, F_OK) == 0) { 

        /* 使用配置文件中的配置 */
        el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck); 
        el::Loggers::addFlag(el::LoggingFlag::DisableApplicationAbortOnFatalLog);
        
        /* Load configuration from file */
    	el::Configurations conf(LOG_CONF_FILE_NAME);
        
    	/* Reconfigure single logger */
    	el::Loggers::reconfigureLogger("default", conf);
    
    	/* Actually reconfigure all loggers instead */ 
    	el::Loggers::reconfigureAllLoggers(conf);        
        
    } else {
        /// 使用默认配置  
        el::Configurations defaultConf;  
        defaultConf.setToDefault();  
        defaultConf.set(el::Level::Global, el::ConfigurationType::Format, "%datetime [%level] %msg");
        defaultConf.set(el::Level::Global, el::ConfigurationType::Enabled, "true");
        defaultConf.set(el::Level::Global, el::ConfigurationType::ToFile, "true");
        defaultConf.set(el::Level::Global, el::ConfigurationType::ToStandardOutput, "false");
        defaultConf.set(el::Level::Global, el::ConfigurationType::Filename, logFileFullPath);
        defaultConf.set(el::Level::Global, el::ConfigurationType::SubsecondPrecision, "6");
        defaultConf.set(el::Level::Global, el::ConfigurationType::PerformanceTracking, "false");
        defaultConf.set(el::Level::Global, el::ConfigurationType::MaxLogFileSize, "209715200");//2097152 2MB / 209715200 200MB / 4398046511104 1GB
        defaultConf.set(el::Level::Trace, el::ConfigurationType::Enabled, "false");
        defaultConf.set(el::Level::Debug, el::ConfigurationType::Enabled, "false");
        
        el::Loggers::reconfigureLogger("default", defaultConf);
        el::Loggers::reconfigureAllLoggers(defaultConf);  
        
        el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck); 
        el::Loggers::addFlag(el::LoggingFlag::DisableApplicationAbortOnFatalLog);
    }

    /* 日志文件超过指定大小后，进行文件转存 */
    el::Helpers::installPreRollOutCallback(rolloutHandler);
    //el::Helpers::setCrashHandler(myCrashHandler);

    return 0;
}

void elog_write(const int mode, const unsigned int Level, const char*filename, unsigned int lineno,const char *msg, ...)
{
    va_list vl_list;  
    va_start(vl_list, msg);  
    char content[1024] = {0};  
    vsnprintf(content, sizeof(content), msg, vl_list);   //格式化处理msg到字符串  
    va_end(vl_list);

    switch (Level) {
        case L_TRACE:
            LOG(TRACE) << "["<<filename<<":"<<lineno<<"]" <<content;
            break;
        case L_DEBUG:
            LOG(DEBUG) << "["<<filename<<":"<<lineno<<"]" << content;
            break;
        case L_INFO:
            LOG(INFO) << "["<<filename<<":"<<lineno<<"]" <<content;
            break;
        case L_WARNING:
            LOG(WARNING) << "["<<filename<<":"<<lineno<<"]" << content;
            break;
        case L_ERROR:
            LOG(ERROR) << "["<<filename<<":"<<lineno<<"]" << content;
            break;
        case L_FATAL:
            LOG(FATAL) << "["<<filename<<":"<<lineno<<"]" << content;
            break;
        default:
            break;
    }
    return;
}



