
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <Windows.h>
#include <limits.h>
#include <math.h>

#include "msgpack.h"
#include "lmsgpack.h"


void _testCUnpack(lua_State* L) {
	int indx = 1;
	if (!lua_isstring(L, indx))
		return 0;
	return lua_msgunpack(L);
}

void main() {
	double d1 = 1.000000000000;
	double d2 = 1.000000000000;
	double ddd = floor(3.1);
	printf(" ++++ %.20f   %f \n", d1, d2);
	if (d1 == d2) {
		printf(" ++++ ok \n");
	}

	printf(" --------------------------------------- \n");
	lua_State* lua_state_;
	lua_state_ = lua_open();
	luaL_openlibs(lua_state_);

	lua_pushcfunction(lua_state_, _testCUnpack);
	lua_setglobal(lua_state_, "_testCUnpack");

	//luaopen_cmsgpack(lua_state_);

	msgpack_sbuffer sbuf;
	msgpack_packer pk;
	msgpack_sbuffer_init(&sbuf);
	msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

	long long context_buff[1024] = { 0 };

	//lua_msgpack_init(lua_state_, &pk, &sbuf);
	reg_msgpack_callback(lua_state_, "msgpack_pack_nil", msgpack_pack_nil);
	reg_msgpack_callback(lua_state_, "msgpack_pack_true", msgpack_pack_true);
	reg_msgpack_callback(lua_state_, "msgpack_pack_false", msgpack_pack_false);
	reg_msgpack_callback(lua_state_, "msgpack_pack_int", msgpack_pack_int);
	reg_msgpack_callback(lua_state_, "msgpack_pack_int64", msgpack_pack_int64);
	reg_msgpack_callback(lua_state_, "msgpack_pack_double", msgpack_pack_double);
	reg_msgpack_callback(lua_state_, "msgpack_pack_str", msgpack_pack_str);
	reg_msgpack_callback(lua_state_, "msgpack_pack_str_body", msgpack_pack_str_body);
	reg_msgpack_callback(lua_state_, "msgpack_pack_map", msgpack_pack_map);
	reg_msgpack_callback(lua_state_, "msgpack_pack_array", msgpack_pack_array);
	reg_msgpack_callback(lua_state_, "msgpack_sbuffer_write", msgpack_sbuffer_write);
	reg_msgpack_callback(lua_state_, "msgpack_sbuffer_clear", msgpack_sbuffer_clear);


	if (luaL_dofile(lua_state_, "D://test.lua")) {
		printf("error %s\n", lua_tostring(lua_state_, -1));
	}
	printf("\n\n\n");

	lua_getglobal(lua_state_, "t");

	char* buff = NULL;
	int len = 0;
	DWORD start, stop;
	start = timeGetTime();
	for (int i = 0; i < 1; ++i) {
		lua_msgpack(lua_state_, -1, &pk, &sbuf);
		printf(" ????????????? %d \n", sbuf.size);
		/*lua_getglobal(lua_state_, "testCUnpack");
		lua_pushlstring(lua_state_, sbuf.data, sbuf.size);

		if (lua_pcall(lua_state_, 1, 1, 0) != 0)
			printf("error \n");
		lua_pop(lua_state_, 1);*/
	}
	/*lua_getglobal(lua_state_, "testCPack");
	if (lua_pcall(lua_state_, 0, 0, 0) != 0)
	printf("error %s \n", lua_tostring(lua_state_, -1));
	lua_pop(lua_state_, 1);*/

	start = timeGetTime();
	for (int i = 0; i < 1; ++i) {
		lua_getglobal(lua_state_, "testCUnpack");
		lua_pushlstring(lua_state_, sbuf.data, sbuf.size);

		if (lua_pcall(lua_state_, 1, 1, 0) != 0)
			printf("error \n");
		lua_pop(lua_state_, 1);
	}
	stop = timeGetTime();
	printf(" ++++ c用时++++  %ld \n", stop - start);

	//start = timeGetTime();
	//for (int i = 0; i < 100000; ++i) {
	//	lua_getglobal(lua_state_, "testUnpack");
	//	lua_pushlstring(lua_state_, sbuf.data, sbuf.size);

	//	if (lua_pcall(lua_state_, 1, 1, 0) != 0)
	//		printf("error \n");
	//	lua_pop(lua_state_, 1);
	//}
	//stop = timeGetTime();
	//printf(" ++++ lua用时++++  %ld \n", stop - start);

	start = timeGetTime();
	/*lua_getglobal(lua_state_, "testC2Pack");
	if (lua_pcall(lua_state_, 0, 0, 0) != 0)
	printf("error %s \n", lua_tostring(lua_state_, -1));
	lua_pop(lua_state_, 1);*/
	stop = timeGetTime();
	//printf(" ++++ c2用时++++  %ld \n", stop - start);

	/*start = timeGetTime();
	lua_getglobal(lua_state_, "testPack");
	if (lua_pcall(lua_state_, 0, 0, 0) != 0)
	printf("error \n");
	lua_pop(lua_state_, 1);
	stop = timeGetTime();
	printf(" ++++ lua用时++++  %ld \n", stop - start);
	*/


	//error(lua_state_, "error running function 'f': %s", lua_tostring(lua_state_, -1));

	printf("\n\n\n");
	//lua_pushnil(lua_state_);
	//while (lua_next(lua_state_, -2))
	//{
	//	// 现在的栈：-1 => value; -2 => key; index => table
	//	// 拷贝一份 key 到栈顶，然后对它做 lua_tostring 就不会改变原始的 key 值了
	//	lua_pushvalue(lua_state_, -2);
	//	// 现在的栈：-1 => key; -2 => value; -3 => key; index => table
	//	unsigned int key = lua_tonumber(lua_state_, -1);
	//	
	//	lua_pop(lua_state_, 2);
	//	printf("------- %d \n", key);
	//}
	//lua_msgpack_free(lua_state_);
	system("pause");
	return;
}
