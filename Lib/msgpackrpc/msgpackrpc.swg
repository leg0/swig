%typemap(out) char const* {
    $result = std::string($1);   
}

// RPC to C++
%typemap(in) SWIGTYPE & {
    $1 = std::get<$num>($input); // get the value from tuple, put it into out parameter
}
// C++ to RPC
%typemap(out) SWIGTYPE & {
    // $result does not need to be sent, only received
}


// TODO:
// convert output parameters into return value
// if there are multiple output parameters, put them into a tuple.

%typemap(ctype) char const* "std::string"

%typemap(ctype) uint8_t  "uint8_t"
%typemap(ctype) uint16_t "uint16_t"
%typemap(ctype) uint32_t "uint32_t"
%typemap(ctype) uint64_t "uint64_t"
%typemap(ctype) int8_t   "int8_t"
%typemap(ctype) int16_t  "int16_t"
%typemap(ctype) int32_t  "int32_t"
%typemap(ctype) int64_t  "int64_t"

%typemap(ctype) bool                "bool"
%typemap(ctype) char                "char"
%typemap(ctype) short               "short"
%typemap(ctype) int                 "int"
%typemap(ctype) long                "long"
%typemap(ctype) long long           "long long"
%typemap(ctype) unsigned char       "unsigned char"
%typemap(ctype) unsigned short      "unsigned short"
%typemap(ctype) unsigned int        "unsigned int"
%typemap(ctype) unsigned            "unsigned"
%typemap(ctype) unsigned long       "unsigned long"
%typemap(ctype) unsigned long long  "unsigned long long"

