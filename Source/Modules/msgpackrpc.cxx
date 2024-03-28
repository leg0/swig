#include "cparse.h"
#include "swigmod.h"

#include <iostream>
#include <string_view>
#include <utility>

using namespace std::literals;

class MSGPACKRPC : public Language {
public:
    void main(int argc, char** argv) override {
        for (int i = 1; i < argc; i++) {
            if (argv[i] == "-client"sv) {
                Swig_mark_arg(i);
                is_client = true;
            }
            else if (argv[i] == "-server"sv) {
                Swig_mark_arg(i);
                is_client = false;
            }
        }
        Preprocessor_define("MSGPACKRPC 1", 0);
        if (is_client)
            Preprocessor_define("MSGPACKRPC_CLIENT 1", 0);
        else
            Preprocessor_define("MSGPACKRPC_SERVER 1", 0);

        SWIG_library_directory("msgpackrpc");
        SWIG_config_file("msgpackrpc.swg");
        directorLanguage();
    }

    int top(Node* n) override {
        module = Getattr(n, "name");

        auto outfile = Getattr(n, "outfile");
        auto f_begin = NewFile(outfile, "w", SWIG_output_files());
        {
            String* moduleHeader = NewStringf("%s.h", module);
            f_header = NewFile(moduleHeader, "w", SWIG_output_files());
            Delete(moduleHeader);
        }

        if (!f_begin) {
          FileErrorDisplay(outfile);
          Exit(EXIT_FAILURE);
        }
        Swig_banner(f_begin);

        if (Swig_directors_enabled()) {
            Printf(f_begin, "#define SWIG_DIRECTORS_ENABLED 1\n");
        }
        else {
            Printf(f_begin, "// #define SWIG_DIRECTORS_ENABLED 1\n");
        }
        if (is_client) {
            f_wrappers = NewStringf(
                            "#include \"msgpackrpc-client.h\"\n"
                            "#include \"%1$s.h\"\n\n"
                            "using namespace %1$s;\n\n",
                            module);
        }
        else {
            f_wrappers = NewString(
                            "#include <msgpackrpc-server.h>\n"
                            "void bind_endpoints(msgpackrpc::server& svr) noexcept {\n");
            // For each non-callback call srv.bind("function_name", [&svr](args...) { return module::function_name(srv, args...); });
        }

        f_runtime = NewString("// --- runtime --- ");
        f_init = NewString("// --- init --- ");
        Printf(f_header, "#pragma once\n"
                         "#include <msgpack.hpp>\n\n");
        f_callbacks = NewString("// --- callbacks --- ");
        Swig_register_filebyname("begin", f_begin);
        Swig_register_filebyname("header", f_header);
        Swig_register_filebyname("wrapper", f_wrappers);
        Swig_register_filebyname("runtime", f_runtime);
        Swig_register_filebyname("init", f_init);

        if (is_client) {
            clientWrapper = NewString("struct RpcClient {\n"
                                      "  RpcClient(msgpackrpc::client& client) noexcept : m_client(client) { }\n");
        }
        // That probably has to happen inside a typemap?
        Language::top(n);
        /* Wrapper_pretty_print(f_init, f_begin); */


        if (is_client) {
            Printf(clientWrapper, "private:\n"
                                  "  msgpackrpc::client& m_client;\n"
                                  "};\n");
        }
        else {
            Printf(f_wrappers, "} // bind_endpoints \n\n");
        }
        /* Printf(f_header, "} // namespace %s\n", module); */

        Dump(f_wrappers, f_begin);

        if (is_client) {
            Dump(clientWrapper, f_header);
        }
        Delete(f_runtime);

        Delete(f_header);
        Delete(f_wrappers);
        Delete(f_init);
        Delete(f_begin);        

        return SWIG_OK;
    }

    static String *ArgList_str(ParmList *p) {
      String *out = NewStringEmpty();
      while (p) {
        String *pstr = Getattr(p, "name");
        Append(out, pstr);
        p = nextSibling(p);
        if (p) {
          Append(out, ", ");
        }
        Delete(pstr);
      }
      return out;
    }

    static bool isCallback(Node * n) {
        return GetFlag(n, "feature:callback");
    }

    int constructorDeclaration(Node *n) override { return SWIG_NOWRAP; }
    int destructorDeclaration(Node *n) override { return SWIG_NOWRAP; }

    String* currentClassMembers = nullptr;
    int classHandler(Node *n) override {
        currentClassMembers = NewStringEmpty();

        Printf(f_header, "struct %s {\n", Getattr(n, "name"));
        Language::classHandler(n);
        Printf(f_header, "  MSGPACK_DEFINE_ARRAY(%s);\n", currentClassMembers);
        Printf(f_header, "};\n");

        Delete(std::exchange(currentClassMembers, nullptr));
        return SWIG_OK;
    }

    int constantWrapper(Node *n) override {
        // TODO: constexpr if possible
        Printf(f_runtime, "inline %s const %s = %s;\n", Getattr(n, "type"), Getattr(n, "name"), Getattr(n, "value"));
        return SWIG_OK;
    }

    int membervariableHandler(Node *n) override {
        char const* sep = "";
        NewStringEmpty();
        if (Len(currentClassMembers) > 0) {
            sep = ", ";
        }
        Printf(currentClassMembers, "%s%s", sep, Getattr(n, "name"));
        Printf(f_header, "  %s %s;\n", Getattr(n, "type"), Getattr(n, "name"));
        return SWIG_OK;
    }

    /* int cDeclaration(Node *n) override { return Language::cDeclaration(n); } */

    int functionWrapper(Node *n) override {
        // Printf(f_wrappers, "/* isCallback=%d */\n", isCallback(n));
        Language::functionWrapper(n);
        if (is_client) {
            return client_functionWrapper(n);
        } else {
            return server_functionWrapper(n);
        }
    }

    int server_functionWrapper(Node *n) {
        auto name = Getattr(n, "sym:name");
        SwigType *type = Getattr(n, "type");
        ParmList *parms  = Getattr(n, "parms");
        String   *parmstr= ParmList_str(parms);
        String   *argstr = ArgList_str(parms);
        String   *func   = SwigType_str(type, NewStringf("%s(%s)", name, parmstr));
        String   *action = Getattr(n, "wrap:action");

        if (isCallback(n)) {
            Printf(f_callbacks, "  %s InvokeCallback_%s(%s) {\n"
                                "    return m_cb.call(\"%s\", %s)->as<%s>();\n"
                                "  }\n", type, name, parmstr, name, argstr, type);
        }
        else {
            Printf(f_wrappers, "  svr.bind(\"%s\", [&svr](%s) {\n", name, parmstr);
            // TODO: use a type map to generate the correct call
            Printf(f_wrappers, "    return %s::%s(svr, %s);\n", module, name, argstr);
            Printf(f_wrappers, "  });\n");
        }

        
        return SWIG_OK;
    }

    int client_functionWrapper(Node *n) {
        // TODO: ignore if it's a constant getter
        

        auto name = Getattr(n, "sym:name");
        SwigType *type = Getattr(n, "type");
        ParmList *parms  = Getattr(n, "parms");
        String   *parmstr= ParmList_str(parms);
        String   *argstr = ArgList_str(parms);
        /* String   *func   = SwigType_str(type, NewStringf("%s(%s)", name, parmstr)); */
        /* String   *action = Getattr(n, "wrap:action"); */

        

        char const* sep = "";
        if (Len(argstr) > 0) {
            sep = ", ";
        }

        // Declaration
        //Printf(clientWrapper, "  %s %s(%s) const;\n", type, name, parmstr);

        // Implementation
        Printf(clientWrapper, "  %s %s(%s) const {\n", type, name, parmstr);
        if (Strcmp(type, "void") == 0) {
            Printf(clientWrapper, "    m_client.call(\"%s\"%s%s);\n", name, sep, argstr);
        } else {
            Printf(clientWrapper, "    return m_client.call(\"%s\"%s%s)->as<%s>();\n", name, sep, argstr, type);
        }
        Printf(clientWrapper, "  }\n");


        return SWIG_OK;
    }

private:
    String *module = nullptr;
    String *clientWrapper = nullptr;
    File *f_begin = nullptr;
    File *f_runtime = nullptr;
    File *f_header = nullptr;
    /* File* header_file = nullptr; */
    File *f_wrappers = nullptr;
    File *f_callbacks = nullptr;
    File *f_init = nullptr;
    bool is_client = false;
};

extern "C" Language* swig_msgpackrpc() {
  return new MSGPACKRPC();
}
