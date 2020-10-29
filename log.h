#ifndef __LOG_L_H__
#define __LOG_L_H__

enum switch_mode  
{  
    mode_minute,  
    mode_hour,  
    mode_day,  
    mode_month,
    mode_butt
};  

int log_init(char *prefix);
void write_cmd(const char *fmt,...);
int write_log(const unsigned int mode, const char *msg, ...);


enum {
    L_TRACE = 0,
    L_DEBUG = 1,
    L_INFO = 2,
    L_WARNING = 3,
    L_ERROR = 4,
    L_FATAL = 5,
};
    
int elog_init(char*logFileFullPath);
void elog_write(const int mode, const unsigned int Level, const char*filename, unsigned int lineno,const char *msg, ...);

#endif
