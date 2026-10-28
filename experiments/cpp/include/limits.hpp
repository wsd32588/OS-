// 你的自定义 limits.h / climits 
#ifndef MY_OS_LIMITS_H
#define MY_OS_LIMITS_H

// 1. 基础整型位数（通常由编译器内置宏直接提供）
#define CHAR_BIT      __CHAR_BIT__

// 2. 有符号字符与无符号字符的极限值
#define SCHAR_MIN     (-__SCHAR_MAX__ - 1)
#define SCHAR_MAX     __SCHAR_MAX__
#define UCHAR_MAX     (__SCHAR_MAX__ * 2 + 1)

// 3. 标准整数类型极限值（自动自适应 32 位或 64 位）
#define INT_MIN       (-__INT_MAX__ - 1)
#define INT_MAX       __INT_MAX__
#define UINT_MAX      (__INT_MAX__ * 2U + 1U)

#define LONG_MIN      (-__LONG_MAX__ - 1L)
#define LONG_MAX      __LONG_MAX__
#define ULONG_MAX     (__LONG_MAX__ * 2UL + 1UL)

// 4. 可以顺便把 MINIX 3 里面关于系统路径、进程数限制的常量也迁过来
#define NAME_MAX      255      // 最大的文件名长度
#define PATH_MAX      1024     // 最大的路径长度

#endif // MY_OS_LIMITS_H
