#ifndef __THREADPOOL_H_
#define __THREADPOOL_H_

/****************************************** 使用实例 begin ***************************************
*
*   线程池初始化，其管理者线程及工作线程都会启动 
*   threadpool_t *thp = threadpool_create(10, 100, 100);
*   printf("threadpool init ... ... \n");
*
*   接收到任务后添加 
*   threadpool_add_task(thp, do_work, (void *)p);
*
*   // ... ...
*
*   销毁 
*   threadpool_destroy(thp);
*
****************************************** 使用实例 end ***************************************/


typedef struct threadpool_t threadpool_t;


/**
* 创建线程池
* @param min_thr_num 线程池中最小线程数
* @param max_thr_num 线程池中最大线程数
* @param queue_max_size 队列能容纳的最大任务数
*/
threadpool_t *threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size, unsigned int thrdFlag=0);

/**
* 向线程池的任务队列中添加一个任务
* @param pool 线程池句柄
* @param void *(*function)(void *arg) 任务处理函数
* @param void *arg 任务处理数据参数
*/
int threadpool_add_task(threadpool_t *pool, void *(*function)(void *arg1, void *arg2, void *arg3), void *arg1, void *arg2, void *arg3);

/**
* 销毁线程池
* @param pool 线程池句柄
*/
int threadpool_destroy(threadpool_t *pool);


#endif
