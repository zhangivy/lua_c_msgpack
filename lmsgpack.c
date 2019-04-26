#include <assert.h>
#include "lstring.h"
#include "ltable.h"
#include <math.h>
#include "lmsgpack.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

static TValue *index2adr(lua_State *L, int idx) {
	if (idx > 0) {
		TValue *o = L->base + (idx - 1);
		api_check(L, idx <= L->ci->top - L->base);
		if (o >= L->top) return cast(TValue *, luaO_nilobject);
		else return o;
	}
	else if (idx > LUA_REGISTRYINDEX) {
		api_check(L, idx != 0 && -idx <= L->top - L->base);
		return L->top + idx;
	}
	else switch (idx) {  /* pseudo-indices */
	case LUA_REGISTRYINDEX: return registry(L);
	case LUA_ENVIRONINDEX: {
		Closure *func = curr_func(L);
		sethvalue(L, &L->env, func->c.env);
		return &L->env;
	}
	case LUA_GLOBALSINDEX: return gt(L);
	default: {
		Closure *func = curr_func(L);
		idx = LUA_GLOBALSINDEX - idx;
		return (idx <= func->c.nupvalues)
			? &func->c.upvalue[idx - 1]
			: cast(TValue *, luaO_nilobject);
	}
	}
}

int(*msgpack_pack_nil)(void*);
int(*msgpack_pack_true)(void*);
int(*msgpack_pack_false)(void*);
int(*msgpack_pack_int)(void*, int);
int(*msgpack_pack_int64)(void*, int64_t);
int(*msgpack_pack_double)(void*, double);
int(*msgpack_pack_str)(void*, size_t);
int(*msgpack_pack_str_body)(void*, const void*, size_t);
int(*msgpack_pack_map)(void*, size_t);
int(*msgpack_pack_array)(void*, size_t);
int(*msgpack_sbuffer_write)(void*, const char*, size_t);
int(*msgpack_sbuffer_clear)(void*);

void lua_msgpack_table(lua_State *L, Table *tt, void* pk, int64_t* context_buff, int context_len);

LUA_API void reg_msgpack_callback(lua_State *L, const char* func_name, void* call_back) {
	if (!strcmp(func_name, "msgpack_pack_nil")) {
		msgpack_pack_nil = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_true")) {
		msgpack_pack_true = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_false")) {
		msgpack_pack_false = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_int")) {
		msgpack_pack_int = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_int64")) {
		msgpack_pack_int64 = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_double")) {
		msgpack_pack_double = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_str")) {
		msgpack_pack_str = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_str_body")) {
		msgpack_pack_str_body = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_map")) {
		msgpack_pack_map = call_back;
	}
	else if (!strcmp(func_name, "msgpack_pack_array")) {
		msgpack_pack_array = call_back;
	}
	else if (!strcmp(func_name, "msgpack_sbuffer_write")) {
		msgpack_sbuffer_write = call_back;
	}
	else if (!strcmp(func_name, "msgpack_sbuffer_clear")) {
		msgpack_sbuffer_clear = call_back;
	}
}

static void get_table_size(Table* tt, int* is_map, int* map_size, int* array_size) {
	*is_map = 0;
	*map_size = 0;
	if (sizenode(tt) > 0) {
		for (int i = 0; i < sizenode(tt); ++i) {
			TValue* o = gval(gnode(tt, i));
			if (ttisnumber(o) || ttisstring(o) || ttistable(o) || ttisboolean(o)) {
				*is_map = 1;
				++(*map_size);
			}
		}
	}
	int array_max_index = 0;
	for (int i = 0; i < tt->sizearray; ++i) {
		TValue* o = &tt->array[i];
		if (ttisnumber(o) || ttisstring(o) || ttistable(o) || ttisboolean(o)) {
			array_max_index = i;
			++(*array_size);
		}
	}
	//数组中有nil 就当成map处理  without_hole
	if (!(*is_map) && (array_max_index + 1) != (*array_size)) {
		*is_map = 1;
	}
}
static void msgpack_number(lua_Number val, void* pk) {
	lua_Number i_val = (lua_Number)floor(val);
	if (i_val != val) {
		//double 或 float
		msgpack_pack_double(pk, val);
	}
	else {
		//char short int long longlong
		if (i_val > INT_MAX || i_val < INT_MIN) {
			msgpack_pack_int64(pk, (int64_t)i_val);
		}
		else {
			msgpack_pack_int(pk, (int)i_val);
		}
	}
}
static void msgpack_table(lua_State *L, Table *tt, void* pk, int is_map, int64_t* context_buff, int context_len) {
	if (sizenode(tt) > 0) {
		for (int i = 0; i < sizenode(tt); ++i) {
			Node* n = gnode(tt, i);
			TValue* o = gval(gnode(tt, i));
			if (ttisnil(o)) {
				continue;
			}
			TValue* k = key2tval(gnode(tt, i));
			if (ttisnumber(k)) {
				msgpack_number(nvalue(k), pk);
			}
			else if (ttisstring(k)) {
				msgpack_pack_str(pk, tsvalue(k)->len);
				msgpack_pack_str_body(pk, svalue(k), tsvalue(k)->len);
			}
			else if (ttistable(k)) {
				lua_msgpack_table(L, hvalue(k), pk, context_buff, context_len);
			}
			else if (ttisboolean(k)) {
				if (bvalue(k)) {
					msgpack_pack_true(pk);
				}
				else {
					msgpack_pack_false(pk);
				}
			}
			else {
				continue; //复杂类型的key 不处理
			}
			if (ttisnumber(o)) {
				msgpack_number(nvalue(o), pk);
			}
			else if (ttisstring(o)) {
				msgpack_pack_str(pk, tsvalue(o)->len);
				msgpack_pack_str_body(pk, svalue(o), tsvalue(o)->len);
			}
			else if (ttistable(o)) {
				lua_msgpack_table(L, hvalue(o), pk, context_buff, context_len);
			}
			else if (ttisboolean(o)) {
				if (bvalue(o)) {
					msgpack_pack_true(pk);
				}
				else {
					msgpack_pack_false(pk);
				}
			}
		}
	}
	for (int i = 0; i < tt->sizearray; ++i) {
		TValue* o = &tt->array[i];
		if (ttisnumber(o)) {
			if (is_map) {
				msgpack_pack_int(pk, i + 1); //lua 数组从1开始
				msgpack_number(nvalue(o), pk);
			}
			else {
				msgpack_number(nvalue(o), pk);
			}
		}
		else if (ttisstring(o)) {
			if (is_map) {
				msgpack_pack_int(pk, i + 1);
				msgpack_pack_str(pk, tsvalue(o)->len);
				msgpack_pack_str_body(pk, svalue(o), tsvalue(o)->len);
			}
			else {
				msgpack_pack_str(pk, tsvalue(o)->len);
				msgpack_pack_str_body(pk, svalue(o), tsvalue(o)->len);
			}
		}
		else if (ttistable(o)) {
			if (is_map) {
				msgpack_pack_int(pk, i + 1);

				lua_msgpack_table(L, hvalue(o), pk, context_buff, context_len);
			}
			else {
				lua_msgpack_table(L, hvalue(o), pk, context_buff, context_len);
			}
		}
		else if (ttisboolean(o)) {
			if (is_map) {
				msgpack_pack_int(pk, i + 1);
				if (bvalue(o)) {
					msgpack_pack_true(pk);
				}
				else {
					msgpack_pack_false(pk);
				}
			}
			else {
				if (bvalue(o)) {
					msgpack_pack_true(pk);
				}
				else {
					msgpack_pack_false(pk);
				}
			}
		}
	}
}
static void lua_msgpack_table(lua_State *L, Table *tt, void* pk, int64_t* context_buff, int context_len) {
	//如果有base_那么序列化base_数据
	const TValue* bt_val = luaH_getstr(tt, luaS_new(L, "base_"));
	if (bt_val != luaO_nilobject && ttistable(bt_val)) {
		tt = hvalue(bt_val);
	}
	for (int i = 0; i < context_len; ++i) {
		if (context_buff[i] == (int64_t)tt) {
			printf("存在递归！！！\n");
			return;
		}
	}
	if (context_len >= 1024) {
		assert(0);
	}
	context_buff[context_len] = (int64_t)tt;
	++context_len;

	int is_map = 0;
	int map_size = 0;
	int array_size = 0;
	get_table_size(tt, &is_map, &map_size, &array_size);
	if (is_map) {
		msgpack_pack_map(pk, map_size + array_size);
	}
	else {
		msgpack_pack_array(pk, array_size);
	}
	msgpack_table(L, tt, pk, is_map, context_buff, context_len);
}

LUA_API void lua_msgpack(lua_State *L, int idx, void* g_pk, void* g_sbuf) {
	StkId t;
	lua_lock(L);
	t = index2adr(L, idx);
	api_check(L, ttistable(t));
	Table *tt = hvalue(t);

	int64_t context_buff[1024] = { 0 };
	int context_len = 0;
	msgpack_sbuffer_clear(g_sbuf);

	lua_msgpack_table(L, tt, g_pk, context_buff, context_len);

	lua_unlock(L);
}



/* ------------------------------- Decoding --------------------------------- */
//使用redis 源码中 lua_cmsgpack
#define MP_CUR_ERROR_NONE   0
#define MP_CUR_ERROR_EOF    1   /* Not enough data to complete operation. */
#define MP_CUR_ERROR_BADFMT 2   /* Bad data format */

typedef struct mp_cur {
	const unsigned char *p;
	size_t left;
	int err;
} mp_cur;

static void mp_cur_init(mp_cur *cursor, const unsigned char *s, size_t len) {
	cursor->p = s;
	cursor->left = len;
	cursor->err = MP_CUR_ERROR_NONE;
}

#define mp_cur_consume(_c,_len) do { _c->p += _len; _c->left -= _len; } while(0)

/* When there is not enough room we set an error in the cursor and return. This
* is very common across the code so we have a macro to make the code look
* a bit simpler. */
#define mp_cur_need(_c,_len) do { \
    if (_c->left < _len) { \
        _c->err = MP_CUR_ERROR_EOF; \
        return; \
    } \
} while(0)

static void memrevifle(void *ptr, size_t len) {
	unsigned char   *p = (unsigned char *)ptr,
		*e = (unsigned char *)p + len - 1,
		aux;
	int test = 1;
	unsigned char *testp = (unsigned char*)&test;

	if (testp[0] == 0) return; /* Big endian, nothing to do. */
	len /= 2;
	while (len--) {
		aux = *p;
		*p = *e;
		*e = aux;
		p++;
		e--;
	}
}

static void mp_decode_to_lua_type(lua_State *L, mp_cur *c);

static void mp_decode_to_lua_array(lua_State *L, mp_cur *c, size_t len) {
	assert(len <= UINT_MAX);
	int index = 1;

	lua_newtable(L);
	while (len--) {
		lua_pushnumber(L, index++);
		mp_decode_to_lua_type(L, c);
		if (c->err) return;
		lua_settable(L, -3);
	}
}

static void mp_decode_to_lua_hash(lua_State *L, mp_cur *c, size_t len) {
	assert(len <= UINT_MAX);
	lua_newtable(L);
	while (len--) {
		mp_decode_to_lua_type(L, c); /* key */
		if (c->err) return;
		mp_decode_to_lua_type(L, c); /* value */
		if (c->err) return;
		lua_settable(L, -3);
	}
}

/* Decode a Message Pack raw object pointed by the string cursor 'c' to
* a Lua type, that is left as the only result on the stack. */
static void mp_decode_to_lua_type(lua_State *L, mp_cur *c) {
	mp_cur_need(c, 1);

	/* If we return more than 18 elements, we must resize the stack to
	* fit all our return values.  But, there is no way to
	* determine how many objects a msgpack will unpack to up front, so
	* we request a +1 larger stack on each iteration (noop if stack is
	* big enough, and when stack does require resize it doubles in size) */
	luaL_checkstack(L, 1,
		"too many return values at once; "
		"use unpack_one or unpack_limit instead.");

	switch (c->p[0]) {
	case 0xcc:  /* uint 8 */
		mp_cur_need(c, 2);
		lua_pushnumber(L, c->p[1]);
		mp_cur_consume(c, 2);
		break;
	case 0xd0:  /* int 8 */
		mp_cur_need(c, 2);
		lua_pushinteger(L, (signed char)c->p[1]);
		mp_cur_consume(c, 2);
		break;
	case 0xcd:  /* uint 16 */
		mp_cur_need(c, 3);
		lua_pushnumber(L,
			(c->p[1] << 8) |
			c->p[2]);
		mp_cur_consume(c, 3);
		break;
	case 0xd1:  /* int 16 */
		mp_cur_need(c, 3);
		lua_pushinteger(L, (int16_t)
			(c->p[1] << 8) |
			c->p[2]);
		mp_cur_consume(c, 3);
		break;
	case 0xce:  /* uint 32 */
		mp_cur_need(c, 5);
		lua_pushnumber(L,
			((uint32_t)c->p[1] << 24) |
			((uint32_t)c->p[2] << 16) |
			((uint32_t)c->p[3] << 8) |
			(uint32_t)c->p[4]);
		mp_cur_consume(c, 5);
		break;
	case 0xd2:  /* int 32 */
		mp_cur_need(c, 5);
		lua_pushinteger(L,
			((int32_t)c->p[1] << 24) |
			((int32_t)c->p[2] << 16) |
			((int32_t)c->p[3] << 8) |
			(int32_t)c->p[4]);
		mp_cur_consume(c, 5);
		break;
	case 0xcf:  /* uint 64 */
		mp_cur_need(c, 9);
		lua_pushnumber(L,
			((uint64_t)c->p[1] << 56) |
			((uint64_t)c->p[2] << 48) |
			((uint64_t)c->p[3] << 40) |
			((uint64_t)c->p[4] << 32) |
			((uint64_t)c->p[5] << 24) |
			((uint64_t)c->p[6] << 16) |
			((uint64_t)c->p[7] << 8) |
			(uint64_t)c->p[8]);
		mp_cur_consume(c, 9);
		break;
	case 0xd3:  /* int 64 */
		mp_cur_need(c, 9);
		lua_pushnumber(L,
			((int64_t)c->p[1] << 56) |
			((int64_t)c->p[2] << 48) |
			((int64_t)c->p[3] << 40) |
			((int64_t)c->p[4] << 32) |
			((int64_t)c->p[5] << 24) |
			((int64_t)c->p[6] << 16) |
			((int64_t)c->p[7] << 8) |
			(int64_t)c->p[8]);
		mp_cur_consume(c, 9);
		break;
	case 0xc0:  /* nil */
		lua_pushnil(L);
		mp_cur_consume(c, 1);
		break;
	case 0xc3:  /* true */
		lua_pushboolean(L, 1);
		mp_cur_consume(c, 1);
		break;
	case 0xc2:  /* false */
		lua_pushboolean(L, 0);
		mp_cur_consume(c, 1);
		break;
	case 0xca:  /* float */
		mp_cur_need(c, 5);
		assert(sizeof(float) == 4);
		{
			float f;
			memcpy(&f, c->p + 1, 4);
			memrevifle(&f, 4);
			lua_pushnumber(L, f);
			mp_cur_consume(c, 5);
		}
		break;
	case 0xcb:  /* double */
		mp_cur_need(c, 9);
		assert(sizeof(double) == 8);
		{
			double d;
			memcpy(&d, c->p + 1, 8);
			memrevifle(&d, 8);
			lua_pushnumber(L, d);
			mp_cur_consume(c, 9);
		}
		break;
	case 0xd9:  /* raw 8 */
		mp_cur_need(c, 2);
		{
			size_t l = c->p[1];
			mp_cur_need(c, 2 + l);
			lua_pushlstring(L, (char*)c->p + 2, l);
			mp_cur_consume(c, 2 + l);
		}
		break;
	case 0xda:  /* raw 16 */
		mp_cur_need(c, 3);
		{
			size_t l = (c->p[1] << 8) | c->p[2];
			mp_cur_need(c, 3 + l);
			lua_pushlstring(L, (char*)c->p + 3, l);
			mp_cur_consume(c, 3 + l);
		}
		break;
	case 0xdb:  /* raw 32 */
		mp_cur_need(c, 5);
		{
			size_t l = ((size_t)c->p[1] << 24) |
				((size_t)c->p[2] << 16) |
				((size_t)c->p[3] << 8) |
				(size_t)c->p[4];
			mp_cur_consume(c, 5);
			mp_cur_need(c, l);
			lua_pushlstring(L, (char*)c->p, l);
			mp_cur_consume(c, l);
		}
		break;
	case 0xdc:  /* array 16 */
		mp_cur_need(c, 3);
		{
			size_t l = (c->p[1] << 8) | c->p[2];
			mp_cur_consume(c, 3);
			mp_decode_to_lua_array(L, c, l);
		}
		break;
	case 0xdd:  /* array 32 */
		mp_cur_need(c, 5);
		{
			size_t l = ((size_t)c->p[1] << 24) |
				((size_t)c->p[2] << 16) |
				((size_t)c->p[3] << 8) |
				(size_t)c->p[4];
			mp_cur_consume(c, 5);
			mp_decode_to_lua_array(L, c, l);
		}
		break;
	case 0xde:  /* map 16 */
		mp_cur_need(c, 3);
		{
			size_t l = (c->p[1] << 8) | c->p[2];
			mp_cur_consume(c, 3);
			mp_decode_to_lua_hash(L, c, l);
		}
		break;
	case 0xdf:  /* map 32 */
		mp_cur_need(c, 5);
		{
			size_t l = ((size_t)c->p[1] << 24) |
				((size_t)c->p[2] << 16) |
				((size_t)c->p[3] << 8) |
				(size_t)c->p[4];
			mp_cur_consume(c, 5);
			mp_decode_to_lua_hash(L, c, l);
		}
		break;
	default:    /* types that can't be idenitified by first byte value. */
		if ((c->p[0] & 0x80) == 0) {   /* positive fixnum */
			lua_pushnumber(L, c->p[0]);
			mp_cur_consume(c, 1);
		}
		else if ((c->p[0] & 0xe0) == 0xe0) {  /* negative fixnum */
			lua_pushinteger(L, (signed char)c->p[0]);
			mp_cur_consume(c, 1);
		}
		else if ((c->p[0] & 0xe0) == 0xa0) {  /* fix raw */
			size_t l = c->p[0] & 0x1f;
			mp_cur_need(c, 1 + l);
			lua_pushlstring(L, (char*)c->p + 1, l);
			mp_cur_consume(c, 1 + l);
		}
		else if ((c->p[0] & 0xf0) == 0x90) {  /* fix map */
			size_t l = c->p[0] & 0xf;
			mp_cur_consume(c, 1);
			mp_decode_to_lua_array(L, c, l);
		}
		else if ((c->p[0] & 0xf0) == 0x80) {  /* fix map */
			size_t l = c->p[0] & 0xf;
			mp_cur_consume(c, 1);
			mp_decode_to_lua_hash(L, c, l);
		}
		else {
			c->err = MP_CUR_ERROR_BADFMT;
		}
	}
}

static int mp_unpack_full(lua_State *L, int limit, int offset) {
	size_t len;
	const char *s;
	mp_cur c;
	int cnt; /* Number of objects unpacked */
	int decode_all = (!limit && !offset);

	s = luaL_checklstring(L, 1, &len); /* if no match, exits */

	if (offset < 0 || limit < 0) /* requesting negative off or lim is invalid */
		return luaL_error(L,
			"Invalid request to unpack with offset of %d and limit of %d.",
			offset, len);
	else if (offset > len)
		return luaL_error(L,
			"Start offset %d greater than input length %d.", offset, len);

	if (decode_all) limit = INT_MAX;

	mp_cur_init(&c, (const unsigned char *)s + offset, len - offset);

	/* We loop over the decode because this could be a stream
	* of multiple top-level values serialized together */
	for (cnt = 0; c.left > 0 && cnt < limit; cnt++) {
		mp_decode_to_lua_type(L, &c);

		if (c.err == MP_CUR_ERROR_EOF) {
			return luaL_error(L, "Missing bytes in input.");
		}
		else if (c.err == MP_CUR_ERROR_BADFMT) {
			return luaL_error(L, "Bad data format in input.");
		}
	}

	if (!decode_all) {
		/* c->left is the remaining size of the input buffer.
		* subtract the entire buffer size from the unprocessed size
		* to get our next start offset */
		int offset = (int)(len - c.left);                                       /* WIN_PORT_FIX cast (int) */
																				/* Return offset -1 when we have have processed the entire buffer. */
		lua_pushinteger(L, c.left == 0 ? -1 : offset);
		/* Results are returned with the arg elements still
		* in place. Lua takes care of only returning
		* elements above the args for us.
		* In this case, we have one arg on the stack
		* for this function, so we insert our first return
		* value at position 2. */
		lua_insert(L, 2);
		cnt += 1; /* increase return count by one to make room for offset */
	}
	lua_pushinteger(L, len - c.left);
	return cnt + 1;
}

LUA_API int lua_msgunpack(lua_State *L) {
	return mp_unpack_full(L, 0, 0);
}