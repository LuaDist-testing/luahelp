/*
 * help.c
 * A simple help system for Lua
 * Author: Luis Carvalho <lexcarvalho at gmail dot com>
 * This code is hereby placed in public domain.
*/

#include <lua.h>
#include <lauxlib.h>
#include <string.h>

#define HELP_LIB_NAME "help"
#define HELP_INFO_NAME "info"
#define HELP_PATH_NAME "path"
#define HELP_PRINT_NAME "print"
#define HELP_LHP "lhp"
#define HELP_LHP_PATT HELP_LHP "/?." HELP_LHP
#define HELP_HEADER "Help"
#define HELP_NA_MSG "Sorry, help not available"
#define HELP_MT_FIELD "__help"

/* Adapted from loadlib.c */

static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");  /* try to open file */
  if (f == NULL) return 0;  /* open failed */
  fclose(f);
  return 1;
}

static const char *pushnexttemplate (lua_State *L, const char *path) {
  const char *l;
  while (*path == *LUA_PATHSEP) path++;  /* skip separators */
  if (*path == '\0') return NULL;  /* no more templates */
  l = strchr(path, *LUA_PATHSEP);  /* find next separator */
  if (l == NULL) l = path + strlen(path);
  lua_pushlstring(L, path, l - path);  /* template */
  return l;
}

static const char *findfile (lua_State *L, const char *name) {
  const char *path;
  name = luaL_gsub(L, name, ".", LUA_DIRSEP);
  name = luaL_gsub(L, name, ":", LUA_DIRSEP); /* method lookup */
  lua_getfield(L, LUA_ENVIRONINDEX, HELP_PATH_NAME);
  path = lua_tostring(L, -1);
  if (path == NULL)
    luaL_error(L, "help." HELP_PATH_NAME " must be a string");
  while ((path = pushnexttemplate(L, path)) != NULL) {
    const char *filename;
    filename = luaL_gsub(L, lua_tostring(L, -1), LUA_PATH_MARK, name);
    lua_remove(L, -2);  /* remove path template */
    if (readable(filename))  /* does file exist and is readable? */
      return filename;  /* return that file name */
    lua_pop(L, 1); /* remove file name */
  }
  return NULL;  /* not found */
}

static int loader_Help (lua_State *L) {
  const char *filename;
  const char *name = lua_tostring(L, 1);
  filename = findfile(L, name);
  if (filename == NULL) return 0;  /* help file not found in this path */
  if (luaL_loadfile(L, filename) != 0)
    luaL_error(L, "error loading help " LUA_QS " from file " LUA_QS ":\n\t%s",
        lua_tostring(L, 1), filename, lua_tostring(L, -1));
  return 1;  /* help file loaded successfully */
}


/* Help interface */

static int help_Help (lua_State *L) {
  luaL_checkstring(L, 1);
  lua_pushboolean(L, 1);
  lua_rawget(L, LUA_ENVIRONINDEX); /* name = cache[true] */
  lua_pushvalue(L, 1);
  lua_rawset(L, LUA_ENVIRONINDEX); /* env[name] = desc */
  return 0;
}

static int help_info (lua_State *L) {
  lua_remove(L, 1); /* remove help object (from __call) */
  if (lua_gettop(L) == 0)
    lua_pushliteral(L, HELP_INFO_NAME);
  lua_pushvalue(L, 1);
  lua_rawget(L, LUA_ENVIRONINDEX);
  if (lua_isnil(L, -1)) { /* not interned? */
    if (!lua_isstring(L, 1)) {
      if (lua_getmetatable(L, 1)) {
        lua_getfield(L, -1, HELP_MT_FIELD);
        if (lua_isnil(L, -1)) lua_pushliteral(L, HELP_NA_MSG);
      }
      else
        lua_pushliteral(L, HELP_NA_MSG);
    }
    else { /* string: read from file */
      lua_pushvalue(L, lua_upvalueindex(1)); /* loader_Help */
      lua_pushvalue(L, 1);
      lua_call(L, 1, 1);
      if (lua_isnil(L, -1)) /* not found? */
        lua_pushliteral(L, HELP_NA_MSG);
      else { /* compiled chunk is at stack top */
        /* setup environment */
        lua_pushboolean(L, 1);
        lua_pushvalue(L, 1); /* name */
        lua_rawset(L, LUA_ENVIRONINDEX); /* cache[true] = name */
        lua_pushvalue(L, lua_upvalueindex(2)); /* chunk env */
        lua_setfenv(L, -2);
        lua_call(L, 0, 0);
        /* push cached description */
        lua_pushvalue(L, 1);
        lua_rawget(L, LUA_ENVIRONINDEX);
      }
    }
  }
  lua_pushvalue(L, lua_upvalueindex(3)); /* help table */
  lua_getfield(L, -1, HELP_PRINT_NAME);
  lua_replace(L, -2);
  lua_insert(L, -2);
  lua_call(L, 1, 0);
  return 0;
}

static int help_register (lua_State *L) {
  luaL_checkstring(L, 2);
  lua_rawset(L, LUA_ENVIRONINDEX);
  return 0;
}

static const luaL_reg help_lib[] = {
  {"register", help_register},
  {NULL, NULL}
};

static void pushhelppath (lua_State *L) {
  const char *p, *d, *s;
  int empty = 1;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");
  s = lua_tostring(L, -1); /* s = package.path */
  lua_pop(L, 2); /* package, package.path */
  while ((d = strstr(s, ";")) != NULL) { /* for each path */
    p = strstr(s, "?.lua");
    if (p != NULL && p < d) { /* match? */
      if (!empty) luaL_addchar(&b, ';');
      luaL_addlstring(&b, s, p - s);
      luaL_addstring(&b, HELP_LHP_PATT);
      empty = 0;
    }
    s = d + 1;
  }
  /* last path */
  if ((p = strstr(s, "?.lua")) != NULL) { /* match? */
    if (!empty) luaL_addchar(&b, ';');
    luaL_addlstring(&b, s, p - s);
    luaL_addstring(&b, HELP_LHP_PATT);
  }
  luaL_pushresult(&b);
}

int luaopen_help (lua_State *L) {
  lua_newtable(L); /* cache */
  lua_newtable(L); /* cache MT */
  lua_pushliteral(L, "k");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, -2); /* cache is weak-keyed */
  lua_replace(L, LUA_ENVIRONINDEX);
  luaL_register(L, HELP_LIB_NAME, help_lib);
  pushhelppath(L);
  lua_setfield(L, -2, HELP_PATH_NAME);
  lua_getglobal(L, "print");
  lua_setfield(L, -2, HELP_PRINT_NAME);
  /* push help_info */
  lua_pushcfunction(L, loader_Help);
  lua_pushvalue(L, -2); /* help */
  lua_setfenv(L, -2); /* env[loader_Help] = help */
  lua_newtable(L); /* chunk env */
  lua_pushcfunction(L, help_Help);
  lua_setfield(L, -2, HELP_HEADER);
  lua_pushvalue(L, -3); /* help */
  lua_pushcclosure(L, help_info, 3);
  /* set metatable */
  lua_newtable(L);
  lua_insert(L, -2);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}

