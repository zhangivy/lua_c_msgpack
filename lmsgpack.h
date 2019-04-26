

#ifndef lmsgpack_h
#define lmsgpack_h

#include "lua.h"

LUA_API void  (reg_msgpack_callback)(lua_State *L, const char* func_name, void* call_back);
LUA_API void  (lua_msgpack)(lua_State *L, int idx, void* g_pk, void* g_sbuf);
LUA_API int  (lua_msgunpack)(lua_State *L);

#endif