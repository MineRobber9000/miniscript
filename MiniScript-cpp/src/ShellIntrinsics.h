//
//  ShellIntrinsics.hpp
//  MiniScript
//
//  Created by Joe Strout on 3/9/19.
//  Copyright © 2021 Joe Strout. All rights reserved.
//

#ifndef SHELLINTRINSICS_H
#define SHELLINTRINSICS_H

#include "MiniScript/MiniscriptTypes.h"
#if _WIN32
	#define useEditline 0
#else
	#include "editline.h"
	#define useEditline 1
#endif

#ifndef __COSMOPOLITAN__
#define DEFAULT_IMPORT_PATH "$MS_SCRIPT_DIR:$MS_SCRIPT_DIR/lib:$MS_EXE_DIR/lib"
#else /* ifdef __COSMOPOLITAN__ */
#define DEFAULT_IMPORT_PATH "$MS_SCRIPT_DIR:$MS_SCRIPT_DIR/lib:$MS_EXE_DIR/lib:/zip/lib"
#endif

extern bool exitASAP;
extern int exitResult;

extern MiniScript::ValueList shellArgs;

void AddPathEnvVars();
void AddScriptPathVar(const char* scriptPartialPath);
void AddShellIntrinsics();

#endif // SHELLINTRINSICS_H
