#include<stdio.h>
#include<malloc.h>
#include<stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "sgg_stack.h"

 
//创建一个空栈
void CreateStack(PSTACK *Stack, unsigned int stackSize, unsigned int elementSize)
{
    unsigned int count = 0;
    *Stack = (STACK*)malloc(sizeof(STACK));
    if (*Stack == NULL)
    {
        printf("新节点空间分配失败！\n");
        exit(-1);
    }
    memset((void*)(*Stack),0,sizeof(STACK));

    (*Stack)->data = (Elementtype*)malloc(stackSize*sizeof(Elementtype));
    if((*Stack)->data == NULL)
    {
        free(*Stack);
        *Stack = NULL;
        exit(-1);
    }
    
    
    while(count < stackSize)
    {
        (*Stack)->data[count] = (Elementtype)malloc(elementSize);
        if ((*Stack)->data[count]==NULL)
        {
            goto end;
        }
        count++;
    }
    
    (*Stack)->top = (*Stack)->buttom = 0;
    (*Stack)->stackSize = stackSize;
    (*Stack)->blockSize = elementSize;    
    return;
end:
    if(*Stack != NULL)
    {
        count = 0;
        while(((*Stack)->data != NULL) && (count < stackSize))
        {
            if((*Stack)->data[count] != NULL)
            {
                free((*Stack)->data[count]);
                (*Stack)->data[count] = NULL;
            }
            count++;
        }
        if((*Stack)->data != NULL)
        {
            free((*Stack)->data);
            (*Stack)->data=NULL;
        }
        free(*Stack);
        *Stack = NULL;
    }
    return;

}

//入栈
int PushStack(PSTACK Stack, Elementtype *val)
{
    static int failedTimes = 0;
    if(Stack==NULL) {
        return -1;
    }
    if(IsStackFull(Stack))
    {
        failedTimes++;
        //printf("Stack is full:%d, top=%d,butt=%d\r\n",failedTimes, Stack->top, Stack->buttom);
        *val = NULL;
        return -1;
    }
    
    //memcpy(Stack->data[Stack->top], val, Stack->blockSize);
    *val = Stack->data[Stack->top];
    Stack->top = ((Stack->top+1)%Stack->stackSize);
    return 0;
}

int BackPushStack(PSTACK Stack)
{
    if(Stack==NULL) {
        return -1;
    }
    if(IsStackEmpty(Stack))
    {
        return -1;
    }
    Stack->top = ((Stack->top+Stack->stackSize-1)%Stack->stackSize);
    return 0;
}

//出栈
int PopStack(PSTACK Stack,Elementtype *val)
{
    if(Stack==NULL) {
        return -1;
    }
    if(IsStackEmpty(Stack))
    {
        *val = NULL;
        return -1;
    }
    //memcpy(val, Stack->data[Stack->buttom], Stack->blockSize);
    *val = Stack->data[Stack->buttom];
    Stack->buttom = ((Stack->buttom+1)%Stack->stackSize);
    return 0;
}

int BackPopStack(PSTACK Stack) {
    if(Stack==NULL) {
        return -1;
    }
    if(IsStackFull(Stack)){
        return -1;
    }
    Stack->buttom = ((Stack->buttom+Stack->stackSize-1)%Stack->stackSize);
    return 0; 
}

bool IsStackEmpty(PSTACK Stack)
{
    if(Stack==NULL) {
        return false;
    }
    if(Stack->top == Stack->buttom)
    {
        return true;
    }
    return false;
}

bool IsStackFull(PSTACK Stack)
{
    if(Stack==NULL) {
        return false;
    }
    if(((Stack->top+1)%((int)Stack->stackSize)) == Stack->buttom)
    {
        return true;
    }
    return false;
}

void DestroyStack(PSTACK *Stack)
{
    unsigned int count = 0;
    if(*Stack != NULL)
    {        
        while(((*Stack)->data != NULL) &&(count < (*Stack)->stackSize))
        {
            if((*Stack)->data[count] != NULL)
            {
                free((*Stack)->data[count]);
                (*Stack)->data[count] = NULL;
            }
            count++;
        }
        if((*Stack)->data != NULL)
        {
            free((*Stack)->data);
            (*Stack)->data=NULL;
        }
        free(*Stack);
        *Stack = NULL;
    }
    return;
}


int GetStackData(PSTACK Stack, Elementtype *val, pfun_StackDataCmp fun, int para)
{
    if(Stack==NULL) {
        return -1;
    }
    int index = Stack->buttom;

    while(Stack->top != index) {
        if(fun(Stack->data[index], para) == 0) {
            *val = Stack->data[index];
            return 0;
        }
        index = ((index+1)%Stack->stackSize);
    }
   
    return -1;
}

int PopStackEx(PSTACK Stack, pfun_StackDataCmp fun, unsigned int para)
{
    if(Stack==NULL) {
        return -1;
    }
    int index = Stack->top;   

    while(Stack->buttom != index) {
        index = ((index+Stack->stackSize -1)%Stack->stackSize);
        if(fun(Stack->data[index], para) == 0) {
            Stack->buttom = ((index+1)%Stack->stackSize);
            return 0;
        }
        
    }
   
    return -2;
}


int TraversalStack(PSTACK Stack, pfun_StackDataProc fun, void*para) {
    if(Stack==NULL) {
        return -1;
    }
    int index = Stack->buttom;  
    while(Stack->top != index) {
        fun(Stack->data[index], Stack->blockSize, para);
        index = ((index+1)%Stack->stackSize);
    }
    return 0;
}

int ClearStack(PSTACK Stack)
{
    if(Stack==NULL) {
        return -1;
    }
     Stack->top = Stack->buttom = 0;
    return 0;
}



