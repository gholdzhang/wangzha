#ifndef __UDP_STACK_H__
#define __UDP_STACK_H__


typedef char* Elementtype;    //    定义数据类型
typedef int (*pfun_StackDataCmp)(char*data, unsigned int para);
typedef void (*pfun_StackDataProc)(char*data, int len, void*para);

//定义栈
typedef struct STACK
{
    Elementtype *data;//数组实现栈内元素定位
    int top;//作为数组下标
    int buttom;
    unsigned int stackSize;
    unsigned int blockSize;
} STACK, *PSTACK;

//    函数声明
void CreateStack(PSTACK *Stack, unsigned int stackSize, unsigned int elementSize);        //    初始化栈
int PushStack(PSTACK Stack, Elementtype *val);        //    入栈函数
int BackPushStack(PSTACK Stack); // 回退一个栈元素
int PopStack(PSTACK Stack,Elementtype *val);        //    出栈函数
int BackPopStack(PSTACK Stack); // 回退一个出栈元素
//void TraverseStack(PSTACK Stack);        //    遍历栈函数
bool IsStackEmpty(PSTACK Stack);        //    判断栈是否为空函数
bool IsStackFull(PSTACK Stack); // 判断栈是否满
void DestroyStack(PSTACK *Stack);       //    清空栈函数
int GetStackData(PSTACK Stack, Elementtype *val, pfun_StackDataCmp fun, int para);
int PopStackEx(PSTACK Stack, pfun_StackDataCmp fun, unsigned int para);
int TraversalStack(PSTACK Stack, pfun_StackDataProc fun, void*para);
int ClearStack(PSTACK Stack);
#endif