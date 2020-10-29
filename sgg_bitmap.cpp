#include "sgg_bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct _Bits {
    char *bits;
    unsigned int length;
};

bits bit_new(unsigned int length)
{
    bits new_bits = (bits)malloc(sizeof(struct _Bits));
    if (new_bits == NULL)
        return NULL;

    int char_nums = sizeof(char) * (length >> 3) + 1;
    new_bits->bits = (char *)malloc(char_nums);
    if (new_bits == NULL) {
        free(new_bits);
        return NULL;
    }
    memset(new_bits->bits, 0, char_nums);
    new_bits->length = length;

    return new_bits;
}

void bit_destroy(bits bit)
{
    if(bit != NULL)
    {
        if(bit->bits != NULL)
        {
            free(bit->bits);
        }        
        free(bit);
    }
    return;    
}

unsigned int bit_length(bits bit)
{
    return bit->length;
}

void bit_set(bits bit, unsigned int pos, unsigned char value)
{
    unsigned char mask = 0x80 >> (pos & 0x7);
    
    if(pos>=bit->length)
        return;
    
    if (value) {
        bit->bits[pos>>3] |= mask;
    } else {
       bit->bits[pos>>3] &= ~mask;
    }
}

char bit_get(bits bit, unsigned int pos)
{
    unsigned char mask = 0x80 >> (pos & 0x7);
    if(pos>=bit->length)
        return 0;

    return (mask & bit->bits[pos>>3]) == mask ? 1 : 0;
}

void bit_clear(bits bit)
{
    int char_nums = sizeof(char) * (bit->length >> 3) + 1;
    memset(bit->bits, 0, char_nums);
    return;
}


unsigned int bit_isAllSet(bits bit, unsigned char value)
{
    unsigned int pos;
    unsigned char val = value & 1;
    for(pos=0; pos<bit->length; pos++){
        if(bit_get(bit,pos) != val)
            return 0;
    }
    return 1;
}

