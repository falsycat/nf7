import json
import subprocess
import sys

# ---- GENERATOR ALGORITHM

# generate Lua to define enum constants
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

# generate Lua API calls to define an imgui function which switches overloads and calls
def gen_func(name, ovlds, indent="", ctxptr=None):
  print(f"{indent}lua_pushcfunction(L, [](auto L) {{")
  print(f"{indent}  (void) L;")
  gen_func_def(name, ovlds, indent=indent+"  ", ctxptr=ctxptr)
  print(f"{indent}}});")
  print(f"{indent}lua_setfield(L, -2, \"{name}\");")
  print()

# generate Lua API calls to push a function which calls a appropriate overload
def gen_func_def(name, ovlds, indent="", ctxptr=None):
  ovld_root = (0 if ctxptr is None else 1, {}, None)
  for ovld in ovlds:
    ovld_node = ovld_root
    for arg in ovld.get("inner", []):
      if "ParmVarDecl" != arg.get("kind"): continue
      tname = get_type_from_argument(arg)
      if tname not in kLuaTypeChecker:
        print(tname, file=sys.stderr)
        break

      if "inner" in arg:
        ovld_node[1]["$"] = (ovld_node[0]+1, {}, ovld)

      checker = kLuaTypeChecker[tname]
      if isinstance(checker, list):
        tnames = checker
      else:
        tnames = [tname]

      for tname in tnames:
        if tname not in ovld_node[1]:
          ovld_node[1][tname] = (ovld_node[0]+1, {}, None)
        ovld_node = ovld_node[1][tname]

    ovld_node[1]["$"] = (ovld_node[0]+1, {}, ovld)

  def _output(ovld_node, depth=0):
    ind = indent + "  "*depth
    if 0 == len(ovld_node[1]):
      gen_func_call(name, ovld_node[0], ovld_node[2], indent=ind, ctxptr=ctxptr)
    elif 1 == len(ovld_node[1]):
      _output(*ovld_node[1].values(), depth=depth)
    else:
      for tname in ovld_node[1]:
        checker = kLuaTypeChecker[tname].replace("#", str(ovld_node[0]+1))
        print(f"{ind}if ({checker}) {{")
        _output(ovld_node[1][tname], depth=depth+1)
        print(f"{ind}}}")
      print(f"{ind}return luaL_error(L, \"unexpected type in param#{ovld_node[0]}\");")

  _output(ovld_root)

# generate Lua to call the function declared by the AST
def gen_func_call(name, narg, item, indent="", ctxptr=None):
  pops   = []
  params = []
  pushes = []
  pcount = 0

  if ctxptr is not None:
    pops.append(
      f"{ctxptr}* const p0 = *reinterpret_cast<{ctxptr}* const*>(luaL_checkudata(L, 1, \"imgui4lua::{ctxptr}\"));")
    pcount = 1

  for arg in item.get("inner", []):
    if "ParmVarDecl" != arg.get("kind"): continue
    if pcount >= narg-1: break

    pop, param, push, n = gen_func_argument(pcount, arg)
    pcount += n
    pops  .extend(pop)
    params.extend(param)
    pushes.extend(push)

  pushes  = gen_func_return(item)
  use_ret = 0 < len(pushes)

  # text output
  nl = "\n"+indent
  cm = ", "
  prefix = "ImGui::" if ctxptr is None else "p0->"
  if 0 < len(pops):
    print(f"{indent}{nl.join(pops)}")
  if use_ret:
    print(f"{indent}const auto r = {prefix}{name}({cm.join(params)});")
  else:
    print(f"{indent}{prefix}{name}({cm.join(params)});")
  if 0 < len(pushes):
    print(f"{indent}{nl.join(pushes)}")
  print(f"{indent}return {len(pushes)};")

# generate Lua to push return values of the function
# returns lines of the source code
def gen_func_return(item):
  ftype = item["type"]["qualType"]
  type  = ftype[0:ftype.find("(")].strip()

  if "void" == type:
    return []
  if "bool" == type:
    return ["lua_pushboolean(L, r);"]
  if type in ["int", "unsigned int", "ImU32"]:
    return ["lua_pushinteger(L, static_cast<lua_Integer>(r));"]
  if "float" == type:
    return ["lua_pushnumber(L, static_cast<lua_Number>(r));"]
  if "ImVec2" == type:
    return [
      "lua_pushnumber(L, static_cast<lua_Number>(r.x));",
      "lua_pushnumber(L, static_cast<lua_Number>(r.y));",
    ]
  if "ImVec4" == type:
    return [
      "lua_pushnumber(L, static_cast<lua_Number>(r.x));",
      "lua_pushnumber(L, static_cast<lua_Number>(r.y));",
      "lua_pushnumber(L, static_cast<lua_Number>(r.z));",
      "lua_pushnumber(L, static_cast<lua_Number>(r.w));",
    ]
  if "ImDrawList *" == type:
    return ["PushImDrawList(L, r);"]

  print(f"unknown return type: {type}", file=sys.stderr)
  return []

# generate Lua to push one of parameters of the function
# returns a tuple with the followings:
#   - string list of sentences to define variable for storing the parameter
#   - string list of expressions to pass to the actual C++ function
#   - string list of sentences to load the parameter
#   - number of parameters to be passed in Lua
def gen_func_argument(pc, item):
  type = item["type"].get("desugaredQualType", item["type"]["qualType"])
  n    = pc+1
  pn   = f"p{pc}"

  if type in ["int", "unsigned int"]:
    return ([f"const int {pn} = static_cast<{type}>(luaL_checkinteger(L, {n}));"], [pn], [], 1)

  if "bool" == type:
    return ([f"const bool {pn} = lua_toboolean(L, {n});"], [pn], [], 1)

  if "float" == type:
    return ([f"const float {pn} = static_cast<float>(luaL_checknumber(L, {n}));"], [pn], [], 1)

  if "const char *" == type:
    return ([f"const char* {pn} = luaL_checkstring(L, {n});"], [pn], [], 1)

  if "const ImVec2 &" == type:
    return ([
        f"const float {pn}_1 = static_cast<float>(luaL_checknumber(L, {n+0}));",
        f"const float {pn}_2 = static_cast<float>(luaL_checknumber(L, {n+1}));",
    ], [f"ImVec2 {{{pn}_1, {pn}_2}}"], [], 2)

  if "const ImVec4 &" == type:
    return ([
        f"const float {pn}_1 = static_cast<float>(luaL_checknumber(L, {n+0}));",
        f"const float {pn}_2 = static_cast<float>(luaL_checknumber(L, {n+1}));",
        f"const float {pn}_3 = static_cast<float>(luaL_checknumber(L, {n+2}));",
        f"const float {pn}_4 = static_cast<float>(luaL_checknumber(L, {n+3}));",
    ], [f"ImVec4 {{{pn}_1, {pn}_2, {pn}_3, {pn}_4}}"], [], 4)

  if "bool *" == type:
    return ([f"bool {pn};"], [f"&{pn}"], [f"lua_pushboolean(L, {pn});"], 1)

  print(f"unknown argument type: {type}", file=sys.stderr)
  return ([], [], [], 0)

def gen_struct(name, funcs):
  print(f"static const auto Push{name} = [](auto L, {name}* ctxptr) {{")
  print(f"  *reinterpret_cast<{name}**>(lua_newuserdata(L, sizeof({name}*))) = ctxptr;")
  print(f"  if (luaL_newmetatable(L, \"imgui4lua::{name}\")) {{")
  print(f"    lua_createtable(L, 0, 0);")
  for fname in funcs:
    print()
    gen_func(fname, funcs[fname], indent="    ", ctxptr=name)
  print(f"    lua_setfield(L, -2, \"__index\");")
  print(f"  }}")
  print(f"  lua_setmetatable(L, -2);")
  print(f"}};")


# ---- GENERATOR UTILITIES

# get a variable type from the AST of an argument
def get_type_from_argument(item):
  return item["type"].get("desugaredQualType", item["type"]["qualType"])


# ---- WALKER DEFINITIONS
class RootWalker:
  def __init__(self):
    self._funcs = {}
    self._enums = {}
    self._structs = {}

  def emit(self):
    for x in self._structs: self._structs[x].emit()
    for x in self._enums: gen_enum(self._enums[x])
    for x in self._funcs: gen_func(x, self._funcs[x])

  def walk(self, item):
    kind = item.get("kind")
    name = item.get("name")
    if "EnumDecl" == kind:
      if name in kEnumWhitelist:
        self._enums[name] = item
    elif "CXXRecordDecl" == kind:
      if name in kStructWhitelist and "inner" in item:
        self._structs[name] = StructWalker(kStructWhitelist[name], item)
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
        f = kFuncWhitelist[name]
        if f is None or f(item):
          if name not in self._funcs:
            self._funcs[name] = [item]
          else:
            self._funcs[name].append(item)
    else:
      self.walk(item)

class StructWalker:
  def __init__(self, whitelist, item):
    self._funcWhitelist = whitelist
    self._name  = item.get("name")
    self._funcs = {}
    self._walk(item)

  def _walk(self, item):
    kind = item.get("kind")
    name = item.get("name")
    if "CXXMethodDecl" == kind:
      if name in self._funcWhitelist:
        if name not in self._funcs:
          self._funcs[name] = [item]
        else:
          self._funcs[name].append(item)
    else:
      for child in item.get("inner", []):
        self._walk(child)

  def emit(self):
    gen_struct(self._name, self._funcs)


# ---- DATA DEFINITIONS
kFuncWhitelist = {
  "Begin": None,
  "End": None,
  "BeginChild": None,
  "EndChild": None,
  "IsWindowAppearing": None,
  "IsWindowCollapsed": None,
  "IsWindowFocused": None,
  "IsWindowHovered": None,
  "GetWindowDrawList": None,
  "GetWindowPos": None,
  "GetWindowSize": None,
  "GetWindowWidth": None,
  "GetWindowHeight": None,
  "SetNextWindowPos": None,
  "SetNextWindowSize": None,
  "SetNextWindowSizeConstraints": None,
  "SetNextWindowContentSize": None,
  "SetNextWindowCollapsed": None,
  "SetNextWindowFocus": None,
  "SetNextWindowScroll": None,
  "SetNextWindowBgAlpha": None,
  "SetWindowPos": None,
  "SetWindowSize": None,
  "SetWindowSizeConstraints": None,
  "SetWindowContentSize": None,
  "SetWindowCollapsed": None,
  "SetWindowFocus": None,
  "SetWindowFontScale": None,
  "GetContentRegionAvail": None,
  "GetContentRegionMax": None,
  "GetWindowContentRegionMin": None,
  "GetWindowContentRegionMax": None,
  "GetBackgroundDrawList": None,
  "GetForegroundDrawList": None,
  "GetScrollX": None,
  "GetScrollY": None,
  "SetScrollX": None,
  "SetScrollY": None,
  "GetScrollMaxX": None,
  "GetScrollMaxY": None,
  "SetScrollHereX": None,
  "SetScrollHereY": None,
  "SetScrollFromPosX": None,
  "SetScrollFromPosY": None,
  #"PushStyleColor": None,
  #"PopStyleColor": None,
  "PushStyleVar": None,
  "PopStyleVar": None,
  "PushTabStop": None,
  "PopTabStop": None,
  "PushButtonRepeat": None,
  "PopButtonRepeat": None,
  "PushItemWidth": None,
  "PopItemWidth": None,
  "SetNextItemWidth": None,
  "CalcItemWidth": None,
  "PushTextWrapPos": None,
  "PopTextWrapPos": None,
  "GetFontSize": None,
  "GetColorU32": None,
  "GetCursorScreenPos": None,
  "SetCursorScreenPos": None,
  "GetCursorPos": None,
  "GetCursorPosX": None,
  "GetCursorPosY": None,
  "SetCursorPos": None,
  "SetCursorPosX": None,
  "SetCursorPosY": None,
  "GetCursorStartPos": None,
  "Separator": None,
  "SameLine": None,
  "NewLine": None,
  "Spacing": None,
  "Dummy": None,
  "Indent": None,
  "Unindent": None,
  "BeginGroup": None,
  "EndGroup": None,
  "AlignTextToFramePadding": None,
  "GetTextLineHeight": None,
  "GetTextLineHeightWithSpacing": None,
  "GetFrameHeight": None,
  "GetFrameHeightWithSpacing": None,
  #"PushID": None,
  #"PopID": None,
  "Text": None,
  "TextColored": None,
  "TextDisabled": None,
  "TextWrapped": None,
  "LabelText": None,
  "BulletText": None,
  "SeparatorText": None,
  "Button": None,
  "SmallButton": None,
  "InvisibleButton": None,
  "ArrowButton": None,
  #"Checkbox": None,
  #"RadioButton": None,
  "ProgressBar": None,
  "Bullet": None,
  "BeginCombo": None,
  "EndCombo": None,
}
kEnumWhitelist = [
  "ImGuiWindowFlags_",
]
kStructWhitelist = {
  "ImDrawList": [
    "PushClipRect",
    "PushClipRectFullScreen",
    "PopClipRect",
    "GetClipRectMin",
    "GetClipRectMax",
    "PushClipRect",
    "PushClipRectFullScreen",
    "PopClipRect",
    #"PushTextureID",
    "PopTextureID",
    "GetClipRectMin",
    "GetClipRectMax",
    "AddLine",
    "AddRect",
    "AddRectFilled",
    "AddRectFilledMultiColor",
    "AddQuad",
    "AddQuadFilled",
    "AddTriangle",
    "AddTriangleFilled",
    "AddCircle",
    "AddCircleFilled",
    "AddNgon",
    "AddNgonFilled",
    "AddEllipse",
    "AddEllipseFilled",
    #"AddText",
    #"AddPolyline",
    #"AddConvexPolyFilled",
    "AddBezierCubic",
    "AddBezierQuadratic",
    #"AddImage",
    #"AddImageQuad",
    #"AddImageRounded",
    "PathClear",
    "PathLineTo",
    "PathLineToMergeDuplicate",
    "PathFillConvex",
    "PathStroke",
    "PathArcTo",
    "PathArcToFast",
    "PathEllipticalArcTo",
    "PathBezierCubicCurveTo",
    "PathBezierQuadraticCurveTo",
    "PathRect",
    "AddDrawCmd",
    "ChannelsSplit",
    "ChannelsMerge",
    "ChannelsSetCurrent",
  ],
}
kLuaTypeChecker = {
  "bool":   "LUA_TBOOLEAN == lua_type(L, #)",
  "number": "LUA_TNUMBER == lua_type(L, #)",
  "str":    "LUA_TSTRING == lua_type(L, #)",
  "$":      "lua_isnone(L, #)",

  "int":            ["number"],
  "unsigned int":   ["number"],
  "float":          ["number"],
  "const char *":   ["str"],
  "const ImVec2 &": ["number", "number"],
  "const ImVec4 &": ["number", "number", "number", "number"],
}


# ---- ENTRYPOINT
proc = subprocess.run(["clang++", "-x", "c++", "-std=c++2b", "-Xclang", "-ast-dump=json", "-fsyntax-only", sys.argv[1]], capture_output=True)
if 0 == proc.returncode:
  walker = RootWalker()
  walker.walk(json.loads(proc.stdout))
  walker.emit()
else:
  print(proc.stderr.decode("utf-8"))
  exit(1)
