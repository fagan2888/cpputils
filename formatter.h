#pragma once
/*
 * A string formatter using iostream.
 *
 * most features of the printf format strings are implemented.
 * integer and string size specifiers like  '%ld', '%zs' or '%ls' are ignored.
 *
 * Usage:
 *   std::cout << string::formatter("%d", 123);
 *
 *   or:
 *
 *   print("%d", 123);
 *
 *
 * (C) 2016 Willem Hengeveld <itsme@xs4all.nl>
 */
#include <sstream>
#include <string>
#include <cstring>      // strchr
#ifdef _WIN32
#include <windows.h>
#endif
#include <vector>
#include <array>

#include "stringconvert.h"
#include "hexdumper.h"

/******************************************************************************
 * add StringFormatter support for various types by implementing a operatoer<<:
 *
 *
 *  windows managed c++  Platform::String, Platform::Guid
 *  windows managed c++ Windows::Storage::Streams::IBuffer
 *  windows GUID
 *  Qt  QString
 *  std::vector, std::array
 *  non-char std::string ( does unicode conversion )
 *  objects containing a 'ToString' method
 *  P
 */
#ifdef _WIN32
#ifdef __cplusplus_winrt 
inline std::ostream& operator<<(std::ostream&os, Platform::String^s)
{
    return os << string::convert<char>(s);
}

template<typename T>
inline std::ostream& operator<<(std::ostream&os, T^h)
{
    if (h)
        return os << h->ToString();
    return os << "(null)";
}
inline std::ostream& operator<<(std::ostream&os, Platform::Guid *h)
{
    if (h)
        return os << h->ToString();
    return os << "(null)";
}
inline std::ostream& operator<<(std::ostream&os, Windows::Storage::Streams::IBuffer^ buf)
{
    Platform::Array<unsigned char> ^bbuf = nullptr;
    Windows::Security::Cryptography::CryptographicBuffer::CopyToByteArray(buf, &bbuf);

    // hexdump buffer bytes
    bool first= true;
    for (unsigned char b : bbuf) {
        if (!first) os << ' ';
        os << std::hex; os.fill('0'); os.width(2);
        os << b;
        first= false;
    }
    return os;
}

inline std::ostream& operator<<(std::ostream&os, const GUID& guid)
{
    os.fill('0');
    os.width(8); os << std::hex << guid.Data1 << '-';
    os.width(4); os << std::hex << guid.Data2 << '-';
    os.width(4); os << std::hex << guid.Data3 << '-';
    os << "-";
    for (int i=0 ; i<8 ; i++) {
        if (i==2) os << '-';
        os.width(2); os << std::hex << (unsigned)guid.Data4[i];
    }
    return os;
}

#endif
#endif

#ifdef QT_VERSION
inline std::ostream& operator<<(std::ostream&os, const QString& s)
{
    QByteArray a = s.toUtf8();

    os << std::string(a.data(), a.data()+a.size());

    return os;
}
#endif

// note: originally i would always hexdump array or vector of unsigned.
// now there is the 'hexdumper' object for that.
//
// any array / vector will now be output as a sequence of items, formatted according to the current
// outputformat.


template<typename T, size_t N>
std::ostream& operator<<(std::ostream&os, const std::array<T,N>& buf)
{
    auto fillchar = os.fill();
    std::ios state(NULL);  state.copyfmt(os);

    bool first= true;
    for (const auto& b : buf) {
        os.copyfmt(state);
        if (!first && fillchar) os << fillchar;
        os << unsigned(b);
        first= false;
    }
    return os;
}
template<typename T, typename A>
std::ostream& operator<<(std::ostream&os, const std::vector<T, A>& buf)
{
    auto fillchar = os.fill();
    std::ios state(NULL);  state.copyfmt(os);

    bool first= true;
    for (const auto& b : buf) {
        os.copyfmt(state);
        if (!first && fillchar) os << fillchar;
        os << unsigned(b);
        first= false;
    }
    return os;
}

// convert all none <char> strings to a char string
// before outputting to the stream
template<typename CHAR>
inline std::enable_if_t<!std::is_same<CHAR,char>::value, std::ostream&> operator<<(std::ostream&os, const std::basic_string<CHAR>& s)
{
    return os << string::convert<char>(s);
}
inline std::ostream& operator<<(std::ostream&os, const wchar_t *s)
{
    return os << string::convert<char>(s);
}

/*****************************************************************************
 *
 */

namespace {
/*
 * some utility templates used for unpacking the parameter pack
 */
template<int ...> struct seq {};

template<int N, int ...S> struct gens : gens<N-1, N-1, S...> {};

template<int ...S> struct gens<0, S...>{ typedef seq<S...> type; };

/*
 * some template utilities, used to determine when to use hexdump
 */
template<typename T>
struct is_container : std::false_type {};

template<typename T>
struct is_container<std::vector<T> > : std::true_type {};
template<typename T>
struct is_container<std::basic_string<T> > : std::true_type {};
template<typename T, int N>
struct is_container<std::array<T,N> > : std::true_type {};

template<typename T>
struct is_hexdumper : std::false_type {};

template<typename T>
struct is_hexdumper<Hex::Hexdumper<T> > : std::true_type {};

}

/*****************************************************************************
 * the StringFormatter class,
 *
 * keeps it's arguments in a tuple, outputs to a ostream when needed.
 *
 */

template<typename...ARGS>
struct StringFormatter {
    const char *fmt;
    std::tuple<ARGS...> args;

    StringFormatter(const char *fmt, ARGS&&...args)
        : fmt(fmt), args(std::forward<ARGS>(args)...)
    {
    }
    StringFormatter(StringFormatter && f)
        : fmt(f.fmt),  args(std::move(f.args))
    {
    }
    friend std::ostream& operator<<(std::ostream&os, const StringFormatter& o)
    {
        o.tostream(os);
        return os;
    }
    void tostream(std::ostream&os) const
    {
        invokeformat(os, typename gens<sizeof...(ARGS)>::type());
    }
    template<int ...S>
    void invokeformat(std::ostream&os, seq<S...>) const
    {
        format(os, fmt, std::get<S>(args) ...);
    }


    //////////////
    // from here on all methods are static.
    //////////////

    // handle case when no params are left.
    static void format(std::ostream&os, const char *fmt) 
    {
        const char *p= fmt;
        while (*p) {
            if (*p=='%') {
                p++;
                if (*p=='%') {
                    os << *p++;
                }
                else {
                    throw std::runtime_error("not enough arguments to format");
                }
            }
            else {
                os << *p++;
            }
        }
    }
    static void applytype(std::ostream& os, char type)
    {
        // unused type/size chars: b k m n r v w y
        switch(type)
        {
            case 'b': // 'b' for Hex::dumper
                // '-':  only hex, otherwise: hex + ascii
                // '0':  no spaces
                break;
            case 'i':
            case 'd':
            case 'u': // unsigned is part of type
                os << std::dec;
                break;
            case 'o':
                os << std::oct;
                break;
            case 'x':
            case 'X':
                os << std::hex;
                break;
            case 'f': // 123.45
            case 'F':
                os << std::fixed;
                break;
            case 'g':  // shortest of 123.45 and 1.23e+2
            case 'G':
                os.unsetf(os.floatfield);
//                os << std::defaultfloat;
                break;
            case 'a':  // hexadecimal floats
            case 'A':
                os.setf(os.fixed | os.scientific, os.floatfield);
//                os << std::hexfloat;
                break;
            case 'e': // 1.23e+2
            case 'E':
                os << std::scientific;
                break;
            case 'c': // char -> need explicit cast
            case 's': // string - from type
                break;
            case 'p': // pointer value - cast to (void*)
                break;
            default:
                throw "unknown format char";
        }
        if ('A'<=type && type<='Z')
                os << std::uppercase;
        else
                os << std::nouppercase;
    }

    template<typename T, typename...FARGS>
    static void format(std::ostream& os, const char *fmt, T& value, FARGS&&...args) 
    {
        bool used_value = false;
        const char *p= fmt;
        while (*p) {
            if (*p=='%') {
                p++;
                if (*p=='%') {
                    os << *p++;
                }
                else {

                    // '-'  means left adjust
                    bool leftadjust= false;
                    if (*p=='-') {
                        p++;
                        leftadjust= true;
                    }
                    bool forcesign= false;
                    //bool blankforpositive= false;
                    if (*p=='+') {
                        p++;
                        forcesign= true;
                    }
                    else if (*p==' ') {
                        p++;
                        //blankforpositive= true;  // <-- todo
                    }
                    
                    // '0' means pad with zero
                    // ',' is useful for arrays
                    char padchar= ' ';

                    if (*p=='0') { p++; padchar='0'; }
                    else if (*p==',') { p++; padchar=','; }

                    // width specification
                    char *q;
                    // todo: support '*'  : take size from argumentlist.
                    // todo: support '#'  : adds 0, 0x, 0X prefix to oct/hex numbers
                    int width= strtol(p, &q, 10);
                    bool havewidth= p!=q;
                    p= q;

                    // precision after '.'
                    int precision= 0;
                    bool haveprecision= false;
                    if (*p=='.') {
                        p++;
                        // todo: support '*'
                        precision= strtol(p, &q, 10);
                        haveprecision= p!=q;
                        p= q;
                    }

                    // ignore argument size field
                    while (std::strchr("qhlLzjt", *p))
                        p++;
                    if (*p=='I') {
                        // microsoft I64 specifier
                        p++;
                        if (p[1]=='6' || p[1]=='4') {
                            p+=2;
                        }
                    }

                    char type= 0;
                    if (*p)
                        type= *p++;

                    // use formatting
                    applytype(os, type);
                    if (forcesign)
                        os << std::showpos;
                    else
                        os << std::noshowpos;

                    if (leftadjust)
                        os << std::left;

                    if (havewidth) {
                        os.width(width);
                        if (!leftadjust)
                            os << std::right;
                    }
                    else {
                        os.width(0);
                    }
                    // todo: support precision(truncate) for strings
                    if (haveprecision)
                        os.precision(precision);
                    os.fill(padchar);

                    if (type=='c')
                        add_wchar(os, value);
                    else if (type=='p')
                        add_pointer(os, value);
                    else if (type=='b')
                        hex_dump_data(os, value);
                    else if (std::strchr("iduoxX", type))
                        output_int(os, value);
                    else
                        os << value;

                    used_value = true;

                    format(os, p, args...);
                    return;
                }
            }
            else {
                os << *p++;
            }
        }

        if (!used_value)
            throw std::runtime_error("too many arguments for format");
    }
    // we need to distinguish real pointers from other types.
    // otherwise the compiler would fail when trying to 
    // cast a non-pointer T to const void*
    template<typename T>
    static std::enable_if_t<std::is_pointer<T>::value, void> add_pointer(std::ostream& os, const T& value) { os << (const void*)value; }
    template<typename T>
    static std::enable_if_t<!std::is_pointer<T>::value, void> add_pointer(std::ostream& os, const T& value) { }

    // make sure we don't call string::convert with non char types.
    template<typename T>
    static std::enable_if_t<std::is_integral<T>::value, void> add_wchar(std::ostream& os, const T& value) { 
        std::basic_string<wchar_t> wc(1, wchar_t(value));
        os << string::convert<char>(wc);
    }
    template<typename T>
    static std::enable_if_t<!std::is_integral<T>::value, void> add_wchar(std::ostream& os, const T& value) { }

    // make sure we call Hex::dumper only for bytevectors or arrays
    template<typename T>
    static std::enable_if_t<!(is_container<T>::value || is_hexdumper<T>::value),void> hex_dump_data(std::ostream& os, const T& value) { }
    template<typename T>
    static std::enable_if_t<is_container<T>::value && std::is_same<typename T::value_type,double>::value,void> hex_dump_data(std::ostream& os, const T& value) { }

    template<typename T>
    static std::enable_if_t<is_container<T>::value && !std::is_same<typename T::value_type,double>::value,void> hex_dump_data(std::ostream& os, const T& value) { 
        if (os.fill()=='0')
            os.fill(0);
        os << std::hex << Hex::dumper(value);
    }

    template<typename T>
    static std::enable_if_t<is_hexdumper<T>::value,void> hex_dump_data(std::ostream& os, const T& value) { 
        if (os.fill()=='0')
            os.fill(0);
        os << std::hex << value;
    }

    template<typename T>
    static std::enable_if_t<!(std::is_integral<T>::value || std::is_floating_point<T>::value),void> output_int(std::ostream& os, T value) { }
    template<typename T>
    static std::enable_if_t<(std::is_integral<T>::value || std::is_floating_point<T>::value) && std::is_signed<T>::value,void> output_int(std::ostream& os, T value) {
        // note: hex or octal numbers are not printed as signed numbers.
        os << (long long)value;
    }
    template<typename T>
    static std::enable_if_t<(std::is_integral<T>::value || std::is_floating_point<T>::value) && std::is_unsigned<T>::value,void> output_int(std::ostream& os, T value) {
        os << (unsigned long long)value;
    }


};

namespace string {
template<typename...ARGS>
auto formatter(const char *fmt, ARGS&&...args)
{
    return StringFormatter<ARGS...>(fmt, std::forward<ARGS>(args)...);
}
}

template<typename...ARGS>
std::string stringformat(const char *fmt, ARGS&&...args)
{
    std::stringstream buf;
    buf << StringFormatter<ARGS...>(fmt, std::forward<ARGS>(args)...);

    return buf.str();
}
template<typename...ARGS>
int fprint(FILE *out, const char *fmt, ARGS&&...args)
{
    auto str = stringformat(fmt, std::forward<ARGS>(args)...);
    return fwrite(str.c_str(), str.size(), 1, out);
}
template<typename...ARGS>
int print(const char *fmt, ARGS&&...args)
{
    return fprint(stdout, fmt, std::forward<ARGS>(args)...);
}

#ifdef QT_VERSION
template<typename...ARGS>
QString qstringformat(const char *fmt, ARGS&&...args)
{
    return QString::fromStdString(stringformat(fmt, std::forward<ARGS>(args)...));
}
#endif

#ifdef _WIN32
template<typename...ARGS>
void debug(const char *fmt, ARGS&&...args)
{
    OutputDebugString(string::convert<TCHAR>(stringformat(fmt, std::forward<ARGS>(args)...).c_str()));
}
#ifdef __cplusplus_winrt 
Platform::String^ ToString()
{
    //String^ str = ref new String();
    return ref new Platform::String(string::convert<wchar_t>(stringformat(fmt, std::forward<ARGS>(args)...)).c_str());
    /*
    IBuffer^ ibuf= WindowsRuntimeBufferExtensions(buf.str().c_str());
    return Windows::Security::Cryptography::CryptographicBuffer::ConvertBinaryToString(BinaryStringEncoding::Utf8, ibuf);
    */
    //return ref new Platform::String(buf.str().c_str(), buf.str().size());
    
}
#endif
#endif


