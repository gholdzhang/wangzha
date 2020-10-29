 /**
  *文件：bit.h
  *目的: 实现bitmap数据结构
  *作者：杜小波
  *联系方式：code2living@gmail.com
  **/
 
 #ifndef _BIT_H_
 #define _BIT_H_
 
 /**
  *存储bitmap的结构体
  *存储的顺序从左至右
  **/
 struct _Bits;
 typedef struct _Bits *bits;
 
 /**
  *获得bitmap
  *@length bitmap的长度
  *@return 所有位都初始化为0的bitmap
  */
 bits bit_new(unsigned int length);
 
 /**
  *销毁一个bitmap
  **/
 void bit_destroy(bits bit);
 
 /**
  *获得y一个bitmap的长度
  *@bit 需要获得长度的bitmap
  *@return bit的长度
  **/
 unsigned int bit_length(bits bit);
 
 /**
  *设置bitmap中相应位置的值
  *@bit 待设置的bitmap
  *@pos  需要设置的位置
  **/
 void bit_set(bits bit, unsigned int pos, unsigned char value);
 
 /**
  *设置bitmap中相应位置的值
  *@bit  待获取的bitmap
  *@pos  获取的位置
  **/
 char bit_get(bits bit, unsigned int pos);

 /**
  *获取bitmap中是否所有的位置都设置了value
  *@bit 待设置的bitmap
  *@value  需要查询的值
  **/
 unsigned int bit_isAllSet(bits bit, unsigned char value);

 /**
  *重置bitmap
  *@bit 待设置的bitmap
  **/
 void bit_clear(bits bit);
 
 #endif /*_BITS_H_*/