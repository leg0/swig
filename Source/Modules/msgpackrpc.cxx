#include "swigmod.h"

#include <iostream>
#include <string_view>

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
    }

    int top(Node* n) override {
        module = Getattr(n, "name");

        f_runtime = NewString("");
        f_init = NewString("");
        f_header = NewString("");
        f_wrappers = NewString("");
        Swig_register_filebyname("begin", f_begin);
        Swig_register_filebyname("header", f_header);
        Swig_register_filebyname("wrapper", f_wrappers);
        Swig_register_filebyname("runtime", f_runtime);
        Swig_register_filebyname("init", f_init);

        auto outfile = Getattr(n, "outfile");
        auto f_begin = NewFile(outfile, "w", SWIG_output_files());
        if (!f_begin) {
          FileErrorDisplay(outfile);
          Exit(EXIT_FAILURE);
        }
        Swig_banner(f_begin);

        if (is_client) {
            Printf(f_begin, "#include <msgpackrpc-client.h>\n");
            Printf(f_begin, "#include <string_view>\n\n");
            Printf(f_begin, "class %s_rpcclient {\n", module);
            Printf(f_begin, "public:\n");
            Printf(f_begin, "  explicit %s_rpcclient(std::string_view unixDomainSocketName) noexcept\n"
                            "      : m_client(unixDomainSocketName) {\n", module);
        }
        else {
            Printf(f_begin, "#include <msgpackrpc-server.h>\n"
                            "void bind_endpoints(msgpackrpc::server& svr) noexcept {\n");
            // For each non-callback call srv.bind("function_name", [&svr](args...) { return module::function_name(srv, args...); });
        }
        // That probably has to happen inside a typemap?
        Language::top(n);
        /* Dump(f_runtime, f_begin); */
        Dump(f_header, f_begin);
        Dump(f_wrappers, f_begin);
        /* Wrapper_pretty_print(f_init, f_begin); */

        if (is_client) {
            Printf(f_begin, "private:\n"
                            "  msgpackrpc::client m_client;\n"
                            "};\n");
        }
        else {
            Printf(f_begin, "} // bind_endpoints \n\n");
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

    int functionWrapper(Node *n) override {
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

        Printf(f_wrappers, "  svr.bind(\"%s\", [&svr](%s) {\n", name, parmstr);
        Printf(f_wrappers, "    return %s::%s(svr, %s);\n", module, name, argstr);
        Printf(f_wrappers, "  });\n");

        return SWIG_OK;
    }

    int client_functionWrapper(Node *n) {
        auto name = Getattr(n, "sym:name");
        SwigType *type = Getattr(n, "type");
        ParmList *parms  = Getattr(n, "parms");
        String   *parmstr= ParmList_str(parms);
        String   *argstr = ArgList_str(parms);
        String   *func   = SwigType_str(type, NewStringf("%s(%s)", name, parmstr));
        String   *action = Getattr(n, "wrap:action");

        // class module::rpcclient {
        //   result_type function_name(params) {
        //      m_client.call("function_name", args)->as<result_type>();
        //   }
        // };
        Printf(f_wrappers, "  %s %s(%s) {\n", type, name, parmstr);
        Printf(f_wrappers, "    return m_client.call(\"%s\", %s)->as<%s>();\n", name, argstr, type);
        Printf(f_wrappers, "  }\n");

        return SWIG_OK;
    }

private:
    String *module = nullptr;
    File *f_begin = nullptr;
    File *f_runtime = nullptr;
    File *f_header = nullptr;
    File *f_wrappers = nullptr;
    File *f_init = nullptr;
    bool is_client = false;
};

extern "C" Language* swig_msgpackrpc() {
  return new MSGPACKRPC();
}
