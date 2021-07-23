//----------------------------------------------------------------------------------------------------------//
//                                                                                                          //
//                                                Scott System                                              //
//                                                                                                          //
//                               (c) Copyright 2006-2007, Scottyu China                                     //
//                                                                                                          //
//                                           All Rights Reserved                                            //
//                                                                                                          //
// File    : error.h                                                                                        //
// By      : scottyu                                                                                        //
// Version : V1.00                                                                                          //
// Time    : 2008-04-14 13:32:00                                                                            //
//                                                                                                          //
//----------------------------------------------------------------------------------------------------------//
// HISTORY                                                                                                  //
//                                                                                                          //
// 1 2008-04-14 13:32:00                                                                                    //
//----------------------------------------------------------------------------------------------------------//


#ifndef ___ERROR__H___
#define ___ERROR__H___  1

#include <stdio.h>
#include <string.h>
#include <memory.h>

#define MSGLEN                      1024

//#define USE_COLOR_CONSOLE         1 //ʹ�ò�ɫ����̨����


#ifndef SCOTT_DEBUG
#define SCOTT_DEBUG
#endif
//------------------------------------------------------------------------------------------------------------
//���������ʽ
//------------------------------------------------------------------------------------------------------------
#ifdef SCOTT_DEBUG
#define CONSOLE_OUTPUT_ERROR        1
#define GUI_OUTPUT_ERROR            2
#define ERROR_OUTPUT                CONSOLE_OUTPUT_ERROR
//#define ERROR_OUTPUT              GUI_OUTPUT_ERROR
#else //SOFT_RELEASE
#endif

///Msg
void Msg(const char *fmt, ...);

//Warn
#ifdef SCOTT_DEBUG
void Warn(const char *fun, const char *file, int line, const char *fmt, ...);
#else //SOFT_RELEASE
#define Warn(x)            {}
#endif

//Err
#ifdef SCOTT_DEBUG
void Err(const char *fun, const char *file, int line, const char *fmt, ...);
#else //SOFT_RELEASE
#define Err(x)             {}
#endif

//Dbg
#ifdef SCOTT_DEBUG
void Dbg(char *fun, char *file, int line, char *fmt, ...);
#else //SOFT_RELEASE
#define Dbg(x)             {}
#endif


//SPRINTF
#ifdef SCOTT_DEBUG
void SPRINTF(char *szErr, char *fmt, ...);
#else //SOFT_RELEASE
#define SPRINTF(x)          {}
#endif

#endif //___ERROR__H___