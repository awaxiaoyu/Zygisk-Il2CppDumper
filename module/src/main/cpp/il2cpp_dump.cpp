//
// Created by Perfare on 2020/7/4.
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;

struct ProcMapEntry {
    uintptr_t start;
    uintptr_t end;
    std::string perms;
    std::string path;
    std::string line;
};

static void *resolve_il2cpp_api(void *handle, const char *name) {
    void *symbol = xdl_sym(handle, name, nullptr);
    if (symbol) {
        return symbol;
    }

    symbol = xdl_dsym(handle, name, nullptr);
    if (symbol) {
        LOGI("api resolved from symtab %s: %p", name, symbol);
        return symbol;
    }

    LOGW("api not found %s", name);
    return nullptr;
}

static bool ensure_dir(const std::string &path) {
    if (mkdir(path.c_str(), 0700) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        return true;
    }
    LOGE("mkdir %s failed: %s", path.c_str(), strerror(errno));
    return false;
}

static bool parse_proc_map_line(const std::string &line, ProcMapEntry *entry) {
    unsigned long long start = 0;
    unsigned long long end = 0;
    char perms[5] = {};
    char path[PATH_MAX] = {};
    int count = sscanf(line.c_str(), "%llx-%llx %4s %*s %*s %*s %1023[^\n]", &start, &end, perms,
                       path);
    if (count < 3) {
        return false;
    }
    entry->start = static_cast<uintptr_t>(start);
    entry->end = static_cast<uintptr_t>(end);
    entry->perms = perms;
    entry->path = count >= 4 ? path : "";
    entry->line = line;
    return true;
}

static std::string strip_deleted_suffix(const std::string &path) {
    constexpr const char *suffix = " (deleted)";
    auto suffix_len = strlen(suffix);
    if (path.size() > suffix_len && path.compare(path.size() - suffix_len, suffix_len, suffix) == 0) {
        return path.substr(0, path.size() - suffix_len);
    }
    return path;
}

static bool contains_path(const std::vector<std::string> &paths, const std::string &path) {
    for (const auto &item: paths) {
        if (item == path) {
            return true;
        }
    }
    return false;
}

static std::vector<ProcMapEntry> write_proc_maps(const std::string &out_path) {
    std::vector<ProcMapEntry> entries;
    std::ifstream in("/proc/self/maps");
    if (!in) {
        LOGE("open /proc/self/maps failed");
        return entries;
    }

    std::ofstream out(out_path);
    std::string line;
    while (std::getline(in, line)) {
        if (out) {
            out << line << "\n";
        }
        ProcMapEntry entry{};
        if (parse_proc_map_line(line, &entry)) {
            entries.push_back(entry);
        }
    }
    return entries;
}

static bool copy_file(const std::string &src_path, const std::string &dst_path) {
    std::ifstream src(src_path, std::ios::binary);
    if (!src) {
        LOGW("open metadata candidate failed: %s", src_path.c_str());
        return false;
    }
    std::ofstream dst(dst_path, std::ios::binary);
    if (!dst) {
        LOGW("create metadata copy failed: %s", dst_path.c_str());
        return false;
    }

    char buffer[8192];
    while (src) {
        src.read(buffer, sizeof(buffer));
        auto read_count = src.gcount();
        if (read_count > 0) {
            dst.write(buffer, read_count);
        }
    }
    return !dst.bad();
}

static void write_symbol_probe_line(std::ofstream &out, void *handle, const char *name) {
    size_t dyn_size = 0;
    void *dynsym = xdl_sym(handle, name, &dyn_size);
    size_t symtab_size = 0;
    void *symtab = xdl_dsym(handle, name, &symtab_size);
    out << name << " dynsym=" << dynsym << " dynsym_size=" << dyn_size << " symtab=" << symtab
        << " symtab_size=" << symtab_size << "\n";
}

static void write_symbol_probe(void *handle, const std::string &path) {
    std::ofstream out(path);
    if (!out) {
        LOGE("create symbol probe failed: %s", path.c_str());
        return;
    }

#define DO_API(r, n, p) write_symbol_probe_line(out, handle, #n);

#include "il2cpp-api-functions.h"

#undef DO_API
}

static void write_il2cpp_maps(const std::vector<ProcMapEntry> &entries, const std::string &path,
                              std::vector<std::string> *metadata_paths) {
    std::ofstream out(path);
    if (!out) {
        LOGE("create il2cpp maps failed: %s", path.c_str());
        return;
    }

    for (const auto &entry: entries) {
        const bool is_il2cpp = entry.line.find("libil2cpp.so") != std::string::npos;
        const bool is_metadata = entry.line.find("global-metadata.dat") != std::string::npos;
        if (!is_il2cpp && !is_metadata) {
            continue;
        }

        out << entry.line << "\n";
        if (is_metadata && metadata_paths) {
            auto metadata_path = strip_deleted_suffix(entry.path);
            if (!metadata_path.empty() && metadata_path[0] == '/' &&
                !contains_path(*metadata_paths, metadata_path)) {
                metadata_paths->push_back(metadata_path);
            }
        }
    }
}

static void write_phdr_info(void *handle, const std::string &path) {
    std::ofstream out(path);
    if (!out) {
        LOGE("create phdr info failed: %s", path.c_str());
        return;
    }

    xdl_info_t info{};
    if (xdl_info(handle, XDL_DI_DLINFO, &info) != 0) {
        out << "xdl_info failed\n";
        return;
    }

    out << "name=" << (info.dli_fname ? info.dli_fname : "") << "\n";
    out << "base=" << info.dli_fbase << "\n";
    out << "phnum=" << info.dlpi_phnum << "\n";
    for (size_t i = 0; i < info.dlpi_phnum; ++i) {
        const auto &phdr = info.dlpi_phdr[i];
        out << "phdr[" << i << "] type=0x" << std::hex << phdr.p_type << " flags=0x"
            << phdr.p_flags << " vaddr=0x" << phdr.p_vaddr << " memsz=0x" << phdr.p_memsz
            << " filesz=0x" << phdr.p_filesz << " offset=0x" << phdr.p_offset << std::dec
            << "\n";
    }
}

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)resolve_il2cpp_api(handle, #n); \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        //TODO attribute
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer - il2cpp_base;
            outPut << " VA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: 0x VA: 0x0";
        }
        /*if (method->slot != 65535) {
            outPut << " Slot: " << std::dec << method->slot;
        }*/
        outPut << "\n\t";
        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        outPut << get_method_modifier(flags);
        //TODO genericContainerIndex
        auto return_type = il2cpp_method_get_return_type(method);
        if (_il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        auto return_class = il2cpp_class_from_type(return_type);
        outPut << il2cpp_class_get_name(return_class) << " " << il2cpp_method_get_name(method)
               << "(";
        auto param_count = il2cpp_method_get_param_count(method);
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }
            auto parameter_class = il2cpp_class_from_type(param);
            outPut << il2cpp_class_get_name(parameter_class) << " "
                   << il2cpp_method_get_param_name(method, i);
            outPut << ", ";
        }
        if (param_count > 0) {
            outPut.seekp(-2, outPut.cur);
        }
        outPut << ") { }\n";
        //TODO GenericInstMethod
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        //TODO attribute
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method(prop);
        auto set = il2cpp_property_get_set_method(prop);
        auto prop_name = il2cpp_property_get_name(prop);
        outPut << "\t";
        Il2CppClass *prop_class = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            prop_class = il2cpp_class_from_type(il2cpp_method_get_return_type(get));
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            auto param = il2cpp_method_get_param(set, 0);
            prop_class = il2cpp_class_from_type(param);
        }
        if (prop_class) {
            outPut << il2cpp_class_get_name(prop_class) << " " << prop_name << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name;
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    auto is_enum = il2cpp_class_is_enum(klass);
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        //TODO attribute
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type(field);
        auto field_class = il2cpp_class_from_type(field_type);
        outPut << il2cpp_class_get_name(field_class) << " " << il2cpp_field_get_name(field);
        //TODO 获取构造函数初始化后的字段值
        if (attrs & FIELD_ATTRIBUTE_LITERAL && is_enum && il2cpp_field_static_get_value) {
            uint64_t val = 0;
            il2cpp_field_static_get_value(field, &val);
            outPut << " = " << std::dec << val;
        }
        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    auto *klass = il2cpp_class_from_type(type);
    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";
    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    //TODO attribute
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << il2cpp_class_get_name(klass); //TODO genericContainerIndex
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_class_get_name(parent));
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        extends.emplace_back(il2cpp_class_get_name(itf));
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (int i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    //TODO EventInfo
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_dump_diagnostics(void *handle, const char *outDir, const char *reason) {
    if (!outDir || outDir[0] == '\0') {
        LOGE("diagnostics output dir is null");
        return;
    }

    auto files_dir = std::string(outDir).append("/files");
    if (!ensure_dir(files_dir)) {
        return;
    }
    auto diag_dir = files_dir + "/il2cpp_diag";
    if (!ensure_dir(diag_dir)) {
        return;
    }

    LOGI("write il2cpp diagnostics: %s", diag_dir.c_str());
    auto maps = write_proc_maps(diag_dir + "/maps.txt");
    std::vector<std::string> metadata_paths;
    write_il2cpp_maps(maps, diag_dir + "/il2cpp_maps.txt", &metadata_paths);
    write_phdr_info(handle, diag_dir + "/phdr.txt");
    write_symbol_probe(handle, diag_dir + "/symbol_probe.txt");

    // Game update note: if a new game moves metadata out of a mapped
    // global-metadata.dat file, extend this diagnostic collector with that
    // game's asset path before writing a new registration resolver.
    std::ofstream summary(diag_dir + "/summary.txt");
    if (summary) {
        summary << "reason=" << (reason ? reason : "unknown") << "\n";
        summary << "diag_dir=" << diag_dir << "\n";
        summary << "metadata_candidates=" << metadata_paths.size() << "\n";
        summary << "next_step=use symbol_probe and il2cpp_maps to add a registration/metadata resolver\n";
    }

    int index = 0;
    for (const auto &metadata_path: metadata_paths) {
        auto dst_path = diag_dir + "/global-metadata-" + std::to_string(index++) + ".dat";
        if (copy_file(metadata_path, dst_path)) {
            LOGI("copied metadata candidate: %s", dst_path.c_str());
        }
    }
}

bool il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);

    bool ok = true;
#define REQUIRE_IL2CPP_API(n)       \
    if (!(n)) {                     \
        LOGE("required api not found %s", #n); \
        ok = false;                 \
    }

    REQUIRE_IL2CPP_API(il2cpp_domain_get_assemblies)
    REQUIRE_IL2CPP_API(il2cpp_is_vm_thread)
    REQUIRE_IL2CPP_API(il2cpp_domain_get)
    REQUIRE_IL2CPP_API(il2cpp_thread_attach)
    REQUIRE_IL2CPP_API(il2cpp_assembly_get_image)
    REQUIRE_IL2CPP_API(il2cpp_image_get_name)
    REQUIRE_IL2CPP_API(il2cpp_class_from_type)
    REQUIRE_IL2CPP_API(il2cpp_class_get_name)
    REQUIRE_IL2CPP_API(il2cpp_class_get_namespace)
    REQUIRE_IL2CPP_API(il2cpp_class_get_flags)
    REQUIRE_IL2CPP_API(il2cpp_class_is_valuetype)
    REQUIRE_IL2CPP_API(il2cpp_class_is_enum)
    REQUIRE_IL2CPP_API(il2cpp_class_get_parent)
    REQUIRE_IL2CPP_API(il2cpp_class_get_type)
    REQUIRE_IL2CPP_API(il2cpp_class_get_interfaces)
    REQUIRE_IL2CPP_API(il2cpp_class_get_fields)
    REQUIRE_IL2CPP_API(il2cpp_field_get_flags)
    REQUIRE_IL2CPP_API(il2cpp_field_get_type)
    REQUIRE_IL2CPP_API(il2cpp_field_get_name)
    REQUIRE_IL2CPP_API(il2cpp_field_get_offset)
    REQUIRE_IL2CPP_API(il2cpp_class_get_properties)
    REQUIRE_IL2CPP_API(il2cpp_property_get_get_method)
    REQUIRE_IL2CPP_API(il2cpp_property_get_set_method)
    REQUIRE_IL2CPP_API(il2cpp_property_get_name)
    REQUIRE_IL2CPP_API(il2cpp_class_get_methods)
    REQUIRE_IL2CPP_API(il2cpp_method_get_flags)
    REQUIRE_IL2CPP_API(il2cpp_method_get_return_type)
    REQUIRE_IL2CPP_API(il2cpp_method_get_name)
    REQUIRE_IL2CPP_API(il2cpp_method_get_param_count)
    REQUIRE_IL2CPP_API(il2cpp_method_get_param)
    REQUIRE_IL2CPP_API(il2cpp_method_get_param_name)
    if (!il2cpp_image_get_class || !il2cpp_image_get_class_count) {
        REQUIRE_IL2CPP_API(il2cpp_get_corlib)
        REQUIRE_IL2CPP_API(il2cpp_class_from_name)
        REQUIRE_IL2CPP_API(il2cpp_class_get_method_from_name)
        REQUIRE_IL2CPP_API(il2cpp_string_new)
        REQUIRE_IL2CPP_API(il2cpp_class_from_system_type)
    }

#undef REQUIRE_IL2CPP_API

    if (!ok) {
        LOGE("Failed to initialize il2cpp api.");
        return false;
    }

    Dl_info dlInfo;
    if (dladdr((void *) il2cpp_domain_get_assemblies, &dlInfo)) {
        il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
    }
    LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);

    // Game update note: if a new version hides more Il2CPP exports, add a resolver here
    // and keep dumping disabled until every required API is resolved.
    for (int i = 0; i < 30 && !il2cpp_is_vm_thread(nullptr); ++i) {
        LOGI("Waiting for il2cpp_init...");
        sleep(1);
    }
    if (!il2cpp_is_vm_thread(nullptr)) {
        LOGE("il2cpp init wait timeout");
        return false;
    }
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get returned null");
        return false;
    }
    if (!il2cpp_thread_attach(domain)) {
        LOGE("il2cpp_thread_attach failed");
        return false;
    }
    return true;
}

void il2cpp_dump(const char *outDir) {
    LOGI("dumping...");
    if (!outDir) {
        LOGE("dump output dir is null");
        return;
    }
    size_t size = 0;
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get returned null while dumping");
        return;
    }
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies) {
        LOGE("il2cpp_domain_get_assemblies returned null");
        return;
    }
    std::stringstream imageOutput;
    for (int i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) {
            continue;
        }
        auto image_name = il2cpp_image_get_name(image);
        if (!image_name) {
            continue;
        }
        imageOutput << "// Image " << i << ": " << image_name << "\n";
    }
    std::vector<std::string> outPuts;
    if (il2cpp_image_get_class && il2cpp_image_get_class_count) {
        LOGI("Version greater than 2018.3");
        //使用il2cpp_image_get_class
        for (int i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) {
                continue;
            }
            std::stringstream imageStr;
            auto image_name = il2cpp_image_get_name(image);
            if (!image_name) {
                continue;
            }
            imageStr << "\n// Dll : " << image_name;
            auto classCount = il2cpp_image_get_class_count(image);
            for (int j = 0; j < classCount; ++j) {
                auto klass = il2cpp_image_get_class(image, j);
                if (!klass) {
                    continue;
                }
                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                if (!type) {
                    continue;
                }
                //LOGD("type name : %s", il2cpp_type_get_name(type));
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);
            }
        }
    } else {
        LOGI("Version less than 2018.3");
        //使用反射
        auto corlib = il2cpp_get_corlib();
        if (!corlib) {
            LOGE("il2cpp_get_corlib returned null");
            return;
        }
        auto assemblyClass = il2cpp_class_from_name(corlib, "System.Reflection", "Assembly");
        if (!assemblyClass) {
            LOGE("System.Reflection.Assembly not found");
            return;
        }
        auto assemblyLoad = il2cpp_class_get_method_from_name(assemblyClass, "Load", 1);
        auto assemblyGetTypes = il2cpp_class_get_method_from_name(assemblyClass, "GetTypes", 0);
        if (assemblyLoad && assemblyLoad->methodPointer) {
            LOGI("Assembly::Load: %p", assemblyLoad->methodPointer);
        } else {
            LOGI("miss Assembly::Load");
            return;
        }
        if (assemblyGetTypes && assemblyGetTypes->methodPointer) {
            LOGI("Assembly::GetTypes: %p", assemblyGetTypes->methodPointer);
        } else {
            LOGI("miss Assembly::GetTypes");
            return;
        }
        typedef void *(*Assembly_Load_ftn)(void *, Il2CppString *, void *);
        typedef Il2CppArray *(*Assembly_GetTypes_ftn)(void *, void *);
        for (int i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) {
                continue;
            }
            std::stringstream imageStr;
            auto image_name = il2cpp_image_get_name(image);
            if (!image_name) {
                continue;
            }
            imageStr << "\n// Dll : " << image_name;
            //LOGD("image name : %s", image->name);
            auto imageName = std::string(image_name);
            auto pos = imageName.rfind('.');
            auto imageNameNoExt = imageName.substr(0, pos);
            auto assemblyFileName = il2cpp_string_new(imageNameNoExt.data());
            auto reflectionAssembly = ((Assembly_Load_ftn) assemblyLoad->methodPointer)(nullptr,
                                                                                        assemblyFileName,
                                                                                        nullptr);
            if (!reflectionAssembly) {
                continue;
            }
            auto reflectionTypes = ((Assembly_GetTypes_ftn) assemblyGetTypes->methodPointer)(
                    reflectionAssembly, nullptr);
            if (!reflectionTypes) {
                continue;
            }
            auto items = reflectionTypes->vector;
            for (int j = 0; j < reflectionTypes->max_length; ++j) {
                if (!items[j]) {
                    continue;
                }
                auto klass = il2cpp_class_from_system_type((Il2CppReflectionType *) items[j]);
                if (!klass) {
                    continue;
                }
                auto type = il2cpp_class_get_type(klass);
                if (!type) {
                    continue;
                }
                //LOGD("type name : %s", il2cpp_type_get_name(type));
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);
            }
        }
    }
    LOGI("write dump file");
    auto outPath = std::string(outDir).append("/files/dump.cs");
    std::ofstream outStream(outPath);
    outStream << imageOutput.str();
    auto count = outPuts.size();
    for (int i = 0; i < count; ++i) {
        outStream << outPuts[i];
    }
    outStream.close();
    LOGI("dump done!");
}
