/*
 * =====================================================================================
 *
 *       Filename:  debug.h
 *
 *    Description:  用于调试输出的头文件，通过宏定义包装 printf 方便调试时输出。具体使用方法为包含这个文件，然后在编译时添加 '-D DEBUG' 选项，就是在调试状态下编译，会显示调用本文件中定义的函数的输出。
 *
 *        Version:  1.0
 *        Created:  2014/04/15 20时11分23秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef __DEBUG_H_
#define __DEBUG_H_

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

#define debug_printf(fmt, ...) \
    do { if (DEBUG_TEST) printf("info in file:%s line:%d func:%s() ==> \n" fmt, \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

#endif
