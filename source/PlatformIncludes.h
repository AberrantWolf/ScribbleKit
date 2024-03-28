//
// Created by Scott Harper on 5/1/2023.
//

#ifndef SCRIBBLEKIT_PLATFORMINCLUDES_H
#define SCRIBBLEKIT_PLATFORMINCLUDES_H


#ifdef WIN32
#include <windows.h>

#define MAIN_ARGS HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow
#define APP_ARGS_DECL HINSTANCE hInstance, int nCmdShow
#define APP_ARGS hInstance, nCmdShow
#define MainFunc() int WINAPI WinMain(MAIN_ARGS)

#else

#define MainFunc() int main()

#endif

#endif //SCRIBBLEKIT_PLATFORMINCLUDES_H
