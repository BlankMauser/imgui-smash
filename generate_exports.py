# Ended up being unused, leaving here just in case

import json

def get_type(ty):
    if "*" in ty:
        ty = ty[:ty.index("*")]
        is_const = "const " in ty
        ty = ty.replace("const ", "").strip()
        ty = map_type_to_rust[ty] if ty in map_type_to_rust else "imgui_bindings::bindings::%s" % ty
        if is_const:
            ty = "*const %s" % ty
        else:
            ty = "*mut %s" % ty
    else:
        ty = ty.replace("const ", "").strip()
        if "[" in ty:
            return get_type("%s*" % ty[:ty.index("[")])
        else:
            ty = map_type_to_rust[ty] if ty in map_type_to_rust else get_type("%s*" % ty)
    return ty

map_to_diff_word = {
    "self": "self_",
    "type": "type_",
    "in": "in_",
    "ref": "ref_"
}

map_type_to_rust = {
    "float": "f32",
    "double": "f64",
    "int": "i32",
    "unsigned int": "u32",
    "char": "i8",
    "bool": "bool",
    "void": "*const i8",
    "size_t": "usize"
}

output = '''

mod internal_extern {
    #[link(name = "imgui_smash")]
    unsafe extern "C" {
        %s        
    }
}

%s

'''

filepath = "D:\\Projects\\Lua\\cimgui\\generator\\output\\definitions.json"
defs = {}
with open(filepath, "r") as f:
    defs = json.load(f)


ignore = [
    "ImGuiFreeType_SetAllocatorFunctions",
    "igPlotHistogram_FnFloatPtr",
    "igPlotLines_FnFloatPtr",
    "ImGuiFreeType_DebugEditFontLoaderFlags"
]

externs = []
exports = []

for main_func_name in defs:
    main_func = defs[main_func_name]
    for func in main_func:
        current_func_name = func["ov_cimguiname"]
        if current_func_name in ignore or current_func_name.startswith("ImVector_"):
            continue
        args = []
        args_name_only = []
        for arg in func["argsT"]:
            arg_name = map_to_diff_word[arg["name"]] if arg["name"] in map_to_diff_word else arg["name"]
            if arg_name == "...":
                args.append("...")
            else:
                args.append("%s: %s" % (arg_name, get_type(arg["type"])))
                args_name_only.append(arg_name)
        final_args = ', '.join(args)
        
        ret = ""

        if "constructor" in func:
            ret = " -> *mut %s" % (get_type(func["stname"]))
        else:
            if "ret" in func and func["ret"] != "void":
                ret = " -> %s" % (get_type(func["ret"]))
        
        func_header = "fn %s(%s)%s" % (current_func_name, final_args, ret)
        
        externs.append("\t\tpub %s;" % func_header) # -> Print function header for extern
        exports.append('''#[no_mangle]
unsafe extern "C" %s {
    internal_extern::%s(%s)
}
        ''' % (func_header, current_func_name, ', '.join(args_name_only)))

print(output % ('\n'.join(externs), '\n'.join(exports)))