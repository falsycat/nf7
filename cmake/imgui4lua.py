import json
import subprocess
import sys

# ---- GENERATOR DEFINITIONS
def gen_func(item):
  name = item["name"]
  if name not in kFuncWhitelist:
    return

  pops   = []
  params = []
  pcount = 0
  for arg in item.get("inner", []):
    pop, param, push = gen_argument(pcount, arg)
    pcount += 1
    pops  .extend(pop)
    params.extend(param)
    push  .extend(push)

  pushes  = gen_return(item)
  use_ret = 0 < len(pushes)

  # text output
  nl = "\n  "
  cm = ", "
  if 0 < len(pops) or 0 < len(pushes):
    print("lua_pushcfunction(L, [](auto L) {")
  else:
    print("lua_pushcfunction(L, [](auto) {")
  if 0 < len(pops):
    print(f"  {nl.join(pops)}")
  if use_ret:
    print(f"  const auto r = ImGui::{name}({cm.join(params)});")
  else:
    print(f"  ImGui::{name}({cm.join(params)});")
  if 0 < len(pushes):
    print(f"  {nl.join(pushes)}")
  print(f"  return {len(pushes)};")
  print("});")
  print(f"lua_setfield(L, -2, \"{name}\");")
  print()

def gen_return(item):
  ftype = item["type"]["qualType"]
  type  = ftype[0:ftype.find("(")-1]

  if "void" == type:
    return []
  if "bool" == type:
    return ["lua_pushboolean(L, r);"]
  if "float" == type:
    return ["lua_pushnumber(L, static_cast<lua_Number>(r));"]
  if "ImVec2" == type:
    return [
        "lua_pushnumber(L, static_cast<lua_Number>(r.x));",
        "lua_pushnumber(L, static_cast<lua_Number>(r.y));",
    ]

  print(f"unknown return type: {type}", file=sys.stderr)
  return []

def gen_argument(pc, item):
  type = item["type"].get("desugaredQualType", item["type"]["qualType"])
  n    = pc+1
  pn   = f"p{pc}"

  if type in ["int", "unsigned int"]:
    return ([f"const int {pn} = static_cast<{type}>(luaL_checkinteger(L, {n}));"], [pn], [])

  if "bool" == type:
    return ([f"const bool {pn} = lua_toboolean(L, {n});"], [pn], [])

  if "const char *" == type:
    return ([f"const char* {pn} = luaL_checkstring(L, {n});"], [pn], [])

  if "const ImVec2 &" == type:
    return ([
        f"const float {pn}_1 = static_cast<float>(luaL_checknumber(L, {n}));",
        f"const float {pn}_2 = static_cast<float>(luaL_checknumber(L, {n}));",
    ], [f"ImVec2 {{{pn}_1, {pn}_2}}"], [])

  if "bool *" == type:
    return ([f"bool {pn};"], [f"&{pn}"], [f"lua_pushboolean(L, {pn});"])

  print(f"unknown argument type: {type}", file=sys.stderr)
  return ([], [], [])


# ---- WALKER DEFINITIONS
def walk_symbols(item):
  kind = item.get("kind")

  if kind == "FunctionDecl":
    gen_func(item)
  else:
    walk(item)

def walk(item):
  kind = item.get("kind")

  w = walk
  if kind == "NamespaceDecl":
    w = walk_symbols

  for child in item.get("inner", []):
    w(child)


# ---- DATA DEFINITIONS
kFuncWhitelist = [
  "Begin",
  "End",
  "BeginChild",
  "EndChild",
  "IsWindowAppearing",
  "IsWindowCollapsed",
  "IsWindowFocused",
  "IsWindowHovered",
  "GetWindowPos",
  "GetWindowSize",
  "GetWindowWidth",
  "GetWindowHeight",
]


# ---- ENTRYPOINT
proc = subprocess.run(["clang++", "-x", "c++", "-std=c++2b", "-Xclang", "-ast-dump=json", "-fsyntax-only", sys.argv[1]], capture_output=True)
if 0 == proc.returncode:
  walk(json.loads(proc.stdout))
else:
  print(proc.stderr.decode("utf-8"))
  exit(1)
