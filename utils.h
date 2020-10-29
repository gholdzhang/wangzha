#ifndef __UTILS_L_H__
#define __UTILS_L_H__
#include "log.h"
#include <sys/time.h>


#define LOG_OF_MIN(fmt,arg...) \
    do {\
        write_log(mode_minute, fmt, ##arg);\
    }while(0);
        
#define LOG_OF_HOUR(fmt,arg...) \
    do {\
        write_log(mode_hour, fmt, ##arg);\
    }while(0);
        
#define LOG_OF_DAY(fmt,arg...) \
    do {\
        write_log(mode_day, fmt, ##arg);\
    }while(0);

#define LOG_OF_MONTH(fmt,arg...) \
    do {\
        write_log(mode_month, fmt, ##arg);\
    }while(0);

#define DEBUG_PRINT(fmt,arg...)\
    do {\
        write_cmd(fmt,##arg);\
    }while(0);

#define ELOG(level,fmt,arg...) \
    do {\
        elog_write(0,level,__FILE__, __LINE__, fmt, ##arg);\
    }while(0);



#define RET_IF(expt,_err) \
    if(expt)\
    { \
        return _err; \
    }

#define RET_VOID_IF(expt) \
    if(expt)\
    { \
        return; \
    }  
#define CONTINUE_IF(expt) \
    if(expt)\
    { \
        continue; \
    }    
#define BREAK_IF(expt) \
        if(expt)\
        { \
            break; \
        } 

#define EXEC_IF(expt,fun) \
            if(expt)\
            { \
                fun; \
            }

#define SL_BITSET(VALUE, MASK)     ((VALUE) = ((VALUE) | (1<<(MASK))))
#define SL_BITCLR(VALUE, MASK)     ((VALUE) = ((VALUE) & (~(1<<(MASK)))))
#define SL_BITGET(VALUE, MASK)     (((VALUE) & (1<<(MASK)))>>(MASK))



 enum{
     NET_TYPE_UDP = 0,
     NET_TYPE_TCP,
     NET_TYPE_BUTT,
 };

  enum{
     NET_LOCATION_CLIENT = 0,
     NET_LOCATION_SERVER,
     NET_LOCATION_BUTT,
 };

 int file_exist_check(char* file);
 int createmsgcomm(key_t msgkey);
 int recivemsgcomm(key_t msgkey, int tid, char *data, int data_len);
 int sendmsgcomm(key_t msgkey, int tid, char *data, int data_len);
 unsigned int htoi(char s[]) ;
 void HexCodePrint(void* data, int len);

template<class T>
class HNode{
public:
	HNode(){
		memset(&data, 0, sizeof(T));
		next = NULL;
	}
	~HNode(){}

	unsigned int key;
	T data;
	HNode*next;
};


template<class T>
class HashBucket{
public:
	HashBucket(int num) :size(0), capacity(num)
	{
		if (capacity <= 100) {
			capacity = 100;
		}
		array = new HNode<T>*[capacity];
		memset(array, 0, sizeof(HNode<T>*)*capacity);
	}

	void ListDestroy(HNode<T> *first)
	{
		HNode<T> *next;
		HNode<T> *cur;
		for (cur = first; cur != NULL; cur = next){
			next = cur->next;
			delete cur;
		}
	}

	~HashBucket(){
		unsigned int i = 0;
		for (i = 0; i < capacity; i++){
			ListDestroy(array[i]);
		}
		delete[] array;
	}

	HNode<T> * HashBucketSearch(unsigned int key){
		unsigned int index = key % capacity;
		HNode<T> *cur = array[index];
		while (cur != NULL) {
			if (cur->key == key) {
				return cur;
			}
			cur = cur->next;
		}
		return NULL;
	}

	void ExpandIfRequired() {
		unsigned int i = 0;
		if (size < capacity) {
			return;
		}
		HNode<T> **new_array;
		new_array = new HNode<T>*[capacity * 2];
		memset(new_array, 0, sizeof(HNode<T>*)*capacity * 2);
		for (i = 0; i < capacity; i++) {
			new_array[i] = array[i];
		}
		delete[] array;
		capacity = capacity * 2;
		array = new_array;
	}

	int HashBucketInsert(unsigned int key, T data)
	{
		//ExpandIfRequired();
		if (HashBucketSearch(key) != NULL){
			return -1;
		}

		unsigned int index = key % capacity;
		HNode<T> *first = array[index];
		HNode<T> *node = new HNode<T>;
		node->key = key;
		memcpy(&node->data, &data, sizeof(T));
		node->next = first;
		array[index] = node;
		size++;

		return 0;
	}
	int HashBucketRemove(unsigned int key)
	{
		unsigned int index = key % capacity;
		HNode<T> *prev = NULL;
		HNode<T> *cur = array[index];
		while (cur != NULL){
			if (cur->key == key){
				if (prev == NULL){
					array[index] = cur->next;
				} else {
					prev->next = cur->next;
				}
				cur->next = NULL;
				delete cur;
				return 0;
			}
			prev = cur;
			cur = cur->next;
		}
		return -1;
	}

	void HashBucketPrint(void){
		unsigned int index = 0;
		HNode<T> *cur;
		for (index = 0; index < capacity;index++) {
			cur = array[index];
			printf("index=%4d\n", index);
			while (cur != NULL) {
				printf("****key=%8d\n", cur->key);
				cur = cur->next;
			}
		}
		
		return ;
	}


private:
	unsigned int size;
	unsigned int capacity;
	HNode<T> **array;
};

 typedef struct {
     struct timeval tv;
     struct timezone tz;
     unsigned long interval;/*µ•ŒªŒ¢√Î*/
     bool isTimeout;
 }_TimerStr,*THandle;
 int addTimer(THandle*handle, unsigned long interval);
 bool isTimerOut(THandle handle);
 void delTimer(THandle*handle);



 #endif
