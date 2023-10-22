import json
import subprocess
import sys

# ---- GENERATOR DEFINITIONS
def gen_enum(item):
  name = item.get("name", "")
  if name not in kEnumWhitelist:
    return

  short_name = name[5:-1]

  print(f"{{  // {name}")
  for child in item.get("inner", []):
    member_name = child["name"]
    if not member_name.startswith(name): continue
    member_short_name = member_name[len(name):]
    print(f"  lua_pushinteger(L, static_cast<lua_Integer>({member_name})); "+
          f"lua_setfield(L, -2, \"{short_name}_{member_short_name}\");")
  print(f"}}  // {name}")
  print()

def gen_func(name, ovlds):
  ovld_root = (0, {}, None)
  for ovld in ovlds:
    ovld_node = ovld_root
    idx       = 0
    for arg in ovld.get("inner", []):
      if "ParmVarDecl" != arg.get("kind"): continue
      tname = get_type_from_argument(arg)
      if tname not in kLuaTypeChecker:
        break
      ovld_node[1][tname] = (idx, {}, None)
      ovld_node = ovld_node[1][tname]
      idx += 1
    ovld_node[1]["$"] = (idx, {}, ovld)

  def _output(ovld_node, depth=1):
    indent = "  "*depth
    if 0 == len(ovld_node[1]):
      gen_single_func(ovld_node[2], indent=indent)
    elif 1 == len(ovld_node[1]):
      _output(*ovld_node[1].values(), depth=depth)
    else:
      for tname in ovld_node[1]:
        checker = kLuaTypeChecker[tname].replace("#", str(ovld_node[0]+1))
        print(f"{indent}if ({checker}) {{")
        _output(ovld_node[1][tname], depth=depth+1)
        print(f"{indent}}}")
      print(f"{indent}return luaL_error(L, \"unexpected type in param#{ovld_node[0]}\");")
  print("lua_pushcfunction(L, [](auto L) {")
  print("  (void) L;")
  _output(ovld_root)
  print("});")
  print(f"lua_setfield(L, -2, \"{name}\");")
  print()

def gen_single_func(item, indent=""):
  name   = item["name"]
  pops   = []
  params = []
  pcount = 0
  for arg in item.get("inner", []):
    if "ParmVarDecl" != arg.get("kind"): continue
    pop, param, push = gen_argument(pcount, arg)
    pcount += 1
    pops  .extend(pop)
    params.extend(param)
    push  .extend(push)

  pushes  = gen_return(item)
  use_ret = 0 < len(pushes)

  # text output
  nl = "\n"+indent
  cm = ", "
  if 0 < len(pops):
    print(f"{indent}{nl.join(pops)}")
  if use_ret:
    print(f"{indent}const auto r = ImGui::{name}({cm.join(params)});")
  else:
    print(f"{indent}ImGui::{name}({cm.join(params)});")
  if 0 < len(pushes):
    print(f"{indent}{nl.join(pushes)}")
  print(f"{indent}return {len(pushes)};")

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


# ---- GENERATOR UTILITIES
def get_type_from_argument(item):
  return item["type"].get("desugaredQualType", item["type"]["qualType"])


# ---- WALKER DEFINITIONS
class Walker:
  def __init__(self):
    self._funcs = {}
    self._enums = {}

  def emit(self):
    for x in self._enums: gen_enum(self._enums[x])
    for x in self._funcs: gen_func(x, self._funcs[x])

  def walk(self, item):
    kind = item.get("kind")
    name = item.get("name")
    if "EnumDecl" == kind:
      if name in kEnumWhitelist:
        self._enums[name] = item
    else:
      w = self.walk
      if "NamespaceDecl" == kind:
        w = self._walk_ns
      for child in item.get("inner", []):
        w(child)

  def _walk_ns(self, item):
    kind = item.get("kind")
    name = item.get("name")
    if "FunctionDecl" == kind:
      if name in kFuncWhitelist:
        if name not in self._funcs:
          self._funcs[name] = [item]
        else:
          self._funcs[name].append(item)
    else:
      self.walk(item)


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
  "Text",
]
kEnumWhitelist = [
  "ImGuiWindowFlags_",
]
kLuaTypeChecker = {
  "int": "LUA_TNUMBER == lua_type(L, #)",
  "const char *": "LUA_TSTRING == lua_type(L, #)",
  "$": "lua_isnone(L, #)",
}


# ---- ENTRYPOINT
proc = subprocess.run(["clang++", "-x", "c++", "-std=c++2b", "-Xclang", "-ast-dump=json", "-fsyntax-only", sys.argv[1]], capture_output=True)
if 0 == proc.returncode:
  walker = Walker()
  walker.walk(json.loads(proc.stdout))
  walker.emit()
else:
  print(proc.stderr.decode("utf-8"))
  exit(1)
