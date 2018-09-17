#ifndef SEGYIO_HPP
#define SEGYIO_HPP

#include <cerrno>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <segyio/segy.h>

/*
 * KNOWN ISSUES AND TODOs:
 *
 * 1. consider strong typedef for traceno, lineno etc.
 * 2. a separate buffer concept, rather than forcing read/write to do it
 * 3. improved naming, especially of final handles
 * 4. slicing support
 * 5. proper line read/write support
 * 6. support for creating files
 * 7. support for imposing or customising geometry
 * 8. add get_at/put_at for bounds-checked on-demand
 */

namespace sio {

namespace detail {

struct segy_file_deleter {
    void operator()( segy_file* fp ) noexcept(true) {
        if( fp ) segy_close( fp );
    }
};

}

namespace detail {

/*
 * segyio uses strong typedefs [1] around all parameters, for two primary
 * reasons:
 *
 * 1. to explicitly document (and enforce) the intention and role of parameters
 * 2. to provide customisation points for skills
 *
 * Essentially, ensures that:
 *
 * file f( "path.sgy"_path ); // compiles
 * file g( "path.sgy" );      // fails
 *
 * The customisation points is described in more detail in the implementation
 *
 * [1] https://foonathan.net/blog/2016/10/19/strong-typedefs.html
 */

/* import swap for ADL */
using std::swap;

template< typename Tag, typename T >
class strong_typedef {
public:
    using value_type = T;

    strong_typedef() = default;

    explicit strong_typedef( const T& x )
        noexcept(std::is_nothrow_copy_constructible< T >::value)
    : value( x ) {}

    explicit strong_typedef( T&& x )
        noexcept(std::is_nothrow_move_constructible< T >::value)
    : value( std::move( x ) ) {}

    explicit operator T&() noexcept(true) {
        return this->value;
    }

    explicit operator const T&() const noexcept(true) {
        return this->value;
    }

    /*
     * Inherit the noexcept property of the underlying swap operation. Usually
     * swap is noexcept (although for strings it's only conditionally after
     * C++17, and not really at all before
     */
    friend void swap( strong_typedef& a, strong_typedef& b )
        noexcept(swap( std::declval< T >(), std::declval< T >() )) {
        swap( static_cast< T& >( a ), static_cast< T& >( b ) );
    }

private:
    T value;
};

}

struct path : detail::strong_typedef< path, std::string > {
    using detail::strong_typedef< path, std::string >::strong_typedef;
};

struct mode : detail::strong_typedef< mode, std::string > {
    using detail::strong_typedef< mode, std::string >::strong_typedef;
    static mode readonly()  { return mode{ "r"  }; }
    static mode readwrite() { return mode{ "r+" }; }
    static mode truncate()  { return mode{ "w+" }; }
};

struct ilbyte : detail::strong_typedef< ilbyte, int > {
    ilbyte() : ilbyte( SEGY_TR_INLINE ) {}
    using detail::strong_typedef< ilbyte, int >::strong_typedef;
};

struct xlbyte : detail::strong_typedef< xlbyte, int > {
    xlbyte() : xlbyte( SEGY_TR_CROSSLINE ) {}
    using detail::strong_typedef< xlbyte, int >::strong_typedef;
};

struct fmt : detail::strong_typedef< fmt , int > {
private:
    using Base = detail::strong_typedef< fmt, int >;

public:
    static fmt ibm()  { return fmt{ SEGY_IBM_FLOAT_4_BYTE };      }
    static fmt ieee() { return fmt{ SEGY_IEEE_FLOAT_4_BYTE };     }
    static fmt int4() { return fmt{ SEGY_SIGNED_INTEGER_4_BYTE }; }
    static fmt int2() { return fmt{ SEGY_SIGNED_SHORT_2_BYTE };   }
    static fmt int1() { return fmt{ SEGY_SIGNED_CHAR_1_BYTE };    }

    fmt() : Base( SEGY_IBM_FLOAT_4_BYTE ) {}
    fmt( int x ) noexcept(false) : Base( x ) {
        switch( x ) {
            case SEGY_IBM_FLOAT_4_BYTE:
            case SEGY_SIGNED_INTEGER_4_BYTE:
            case SEGY_SIGNED_SHORT_2_BYTE:
            case SEGY_FIXED_POINT_WITH_GAIN_4_BYTE:
            case SEGY_IEEE_FLOAT_4_BYTE:
            case SEGY_SIGNED_CHAR_1_BYTE:
                return;

            case SEGY_NOT_IN_USE_1:
            case SEGY_NOT_IN_USE_2:
            default:
                throw std::invalid_argument(
                    "unknown format specifier key " + std::to_string(x)
                );
        }
    }

    const char* description() const noexcept(true) {
        switch( int( *this ) ) {
            case SEGY_IBM_FLOAT_4_BYTE:
                return "ibm float";

            case SEGY_SIGNED_INTEGER_4_BYTE:
                return "int";

            case SEGY_SIGNED_SHORT_2_BYTE:
                return "short";

            case SEGY_FIXED_POINT_WITH_GAIN_4_BYTE:
                return "fixed-point float with gain";

            case SEGY_IEEE_FLOAT_4_BYTE:
                return "ieee float";

            case SEGY_SIGNED_CHAR_1_BYTE:
                return "byte";

            case SEGY_NOT_IN_USE_1:
            case SEGY_NOT_IN_USE_2:
            default:
                return "unknown";
        }
    }
};

struct sorting : detail::strong_typedef< sorting, int > {
private:
    using Base = detail::strong_typedef< sorting, int >;

public:
    static sorting iline() { return sorting{ SEGY_INLINE_SORTING    }; };
    static sorting xline() { return sorting{ SEGY_CROSSLINE_SORTING }; };

    sorting() : Base( SEGY_INLINE_SORTING ) {}
    sorting( int x ) noexcept(false) : Base( x ) {
        switch( x ) {
            case SEGY_INLINE_SORTING:
            case SEGY_CROSSLINE_SORTING:
                return;

            case SEGY_UNKNOWN_SORTING:
            default:
                throw std::invalid_argument(
                    "unknown sorting specifier " + std::to_string(x)
                );
        }
    }

    const char* description() const noexcept(true) {
        switch( int(*this) ) {
            case SEGY_INLINE_SORTING:
                return "inline";

            case SEGY_CROSSLINE_SORTING:
                return "crossline";

            case SEGY_UNKNOWN_SORTING:
            default:
                return "unknown";
        }
    }
};

namespace literals {

path operator"" _path( const char* name, std::size_t ) {
    return path{ name };
}

mode operator"" _mode( const char* name, std::size_t ) {
    return mode{ name };
}

ilbyte operator"" _il( unsigned long long x ) {
    return ilbyte{ int( x ) };
}

xlbyte operator"" _xl( unsigned long long x ) {
    return xlbyte{ int( x ) };
}

fmt operator"" _fmt( unsigned long long x ) {
    return fmt{ int( x ) };
}

}

namespace {

/*
 * This (unavailable) namespace implements the main dispatch mechanism for
 * customisation points, i.e. where control is given to skills (mixins).
 *
 * Skills implement custom behaviour for a customisation point by implementing
 * the operator() for a specific type
 *
 * Once you strip away all the line noise that is C++ template metaprogramming,
 * it's a fairly simple procecure: at a customisation point, look at every
 * skill, and execute operator() if it exists. In code:
 *
 * type = customisation-point.type
 * for skill in skills:
 *     if skill.has-overload(type):
 *         skill.call(type)
 *
 * This meta program is run at compile time. If a skill does not define a
 * (public) operator() for that particular type, it calls a no-op substitute
 * function, which will be killed by the optimiser.
 *
 * Use the strong_typedef in sio::detail to create new types for customisation
 * points, if it is based on a simpler type (like std::string).
 */

/*
 * invocable is an emulation of the C++17 std::is_invocable
 *
 * For our purposes, it's the check: does F.operator()( Args... ) exist?
 */
template< typename F, typename... Args >
using invocable = std::is_constructible<
    std::function< void( Args &&... ) >,
    std::reference_wrapper< typename std::remove_reference< F >::type >
>;

template< typename F, typename... Arg >
using enable_if_invocable  = std::enable_if<  invocable< F, Arg... >::value >;

template< typename F, typename... Arg >
using disable_if_invocable = std::enable_if< !invocable< F, Arg... >::value >;

/*
 * if exists F.operator()(Args) exists, call it
 */
template< typename F, typename... Args >
typename enable_if_invocable< F, Args... >::type
call_if_exists( F& f, Args&& ... x ) { f( std::forward< Args >( x ) ... ); }

/*
 * if not, just do nothing and let the optimiser kill it
 */
template< typename F, typename... Arg >
typename disable_if_invocable< F, Arg... >::type
call_if_exists( F&, Arg && ... ) {}

/*
 * the template< typename > class... trick allows multiple sets of parameter
 * packs. otherwise, the first paramter pack would always swallow the remaining
 * template parameters. This adds support for consider() with multiple
 * arguments.
 */
template< template< typename > class... Skills, class Base, typename... Args >
void apply_all( Base& f, Args && ... args ) {
    /*
     * "for-each-argument"
     * https://isocpp.org/blog/2015/01/for-each-argument-sean-parent
     *
     * This fenomenal trick walks the list of skills and either calls the
     * approperate F.operator(), or sets the no-op.
     *
     * In essence, it casts the base to every mixin and lets the invocable
     * machinery figure out what to do with it (to inject operator() or a
     * no-op). In C++17 this can pretty much be reduced to a fold expression.
     *
     * Skills are *always* called left-to-right [1], so if operations have some
     * dependency it is *crucial* that all dependencies are to the left in the
     * skill-list.
     *
     * [1] this is guaranteed by the int[] aggregate, see the isocpp link for
     * details.
     */

    using arrayt = int[];
    static_cast<void>(arrayt{( call_if_exists(static_cast< Skills< Base >& >(f),
                               std::forward< Args >( args ) ... ), 0) ... });
}

}

namespace {
/*
 * backport std::conjunction and std::disjunction from C++17, if unavailable,
 * needed for has_any and has_all
 *
 * https://en.cppreference.com/w/cpp/types/conjunction
 * https://en.cppreference.com/w/cpp/types/disjunction
 */

#ifdef __cpp_lib_logical_traits

    using std::conjunction;
    using std::disjunction;

#else

    template< typename... > struct conjunction : std::true_type {};
    template< typename B1 > struct conjunction< B1 > : B1 {};
    template< typename B1, class... Bn >
    struct conjunction< B1, Bn... >
        : std::conditional<bool(B1::value), conjunction<Bn...>, B1>::type {};

    template< typename... > struct disjunction : std::false_type {};
    template< typename B1 > struct disjunction< B1 > : B1 {};
    template< typename B1, class... Bn >
    struct disjunction< B1, Bn... >
        : std::conditional<bool(B1::value), B1, disjunction<Bn...>>::type {};

#endif

}

/*
 * Check if the file handle has any or all of the mentioned skills, useful for
 * compile-time selection of overloads or querying features
 */
template< typename File, template< typename > typename... Skills >
using all_skills = conjunction< std::is_base_of< Skills< File >, File > ...  >;

template< typename File, template< typename > typename... Skills >
using any_skills = disjunction< std::is_base_of< Skills< File >, File > ...  >;

/*
 * CONFIG
 */

struct config {
    sio::mode mode = sio::mode::readonly();

    ilbyte iline;
    xlbyte xline;

    /* in C++14 could be tuple with get< type > */

    config& with( sio::mode x ) { this->mode  = x; return *this; }
    config& with( ilbyte x )    { this->iline = x; return *this; }
    config& with( xlbyte x )    { this->xline = x; return *this; }
};

/*
 * The stupid basic file that keeps on learning new skills
 *
 * The segyio C++ file handle is based on variadic CRTP [1], which in short
 * means a stupid base class with a bunch of template parameters that
 * encapsulate and describe a certain behaviour, restriction, or guarantee.
 *
 * The motivation for this is to provide fine-grained static guarantees for
 * file handles, such as:
 *
 * - this file MUST be a cube
 * - this file is *never* writable
 * - this file is *always* writable
 * - this file handle cannot be copied by accident (is unique)
 * - as long as this handle lives, the file is open (always-alive)
 *
 * All these properties can be made and proven staticially, and means users
 * never really have to check at runtime (at least manually). Aditionally,
 * users can implement their own custom restrictions and new behaviours, and
 * add at their leisure.
 *
 * To teach the file a new skill, write any class and add it to the template
 * parameter list of basic_path. Any public method will be available in the
 * resulting file handle.
 *
 * Syntactically this is all pretty heavy, but the end-user experience should
 * be nice, and this shouldn't be visible at all
 *
 * [1] https://www.fluentcpp.com/2018/06/22/variadic-crtp-opt-in-for-class-features-at-compile-time/
 */
template< template< typename... > class ... Skills >
class basic_file : public Skills< basic_file< Skills... > >... {
public:
    /*
     * method version of all/any skills. Useful when you're in a context where
     * an instance is already available, and a quite natural way of checking
     * for either features or that requirements are maintained.
     *
     * please note that the method must be called as
     * f.template all_skills<Skills...> to enforce second-phase lookup.
     *
     * Example use:
     *
     * template< typename Derived >
     * struct Skill {
     *      void foo() {
     *          auto* self = static_cast< Derived >( this );
     *          static_assert( self->template know_all< Skill1, Skill2 >(),
     *                         "foo requires Skill1 (bar) and Skill2 (baz)" );
     *          self->bar();
     *          self->baz();
     *     }
     * };
     */
    template< template< typename > typename... Skill >
    static constexpr bool know_all() {
        return all_skills< basic_file, Skill... >::value;
    }

    template< template< typename > typename... Skill >
    static constexpr bool know_any() {
        return any_skills< basic_file, Skill... >::value;
    }

    basic_file() = default;

    /*
     * The initialisation list is a mega hack
     *
     * The problem is skills that disallows default construction of a file
     * handle, or in some other way mess with the default constructor.
     * Disabling default construction is an attractive property, because it
     * means that if a program compiles then it *has* to always open a file on
     * object initalisation, and if it constructs without throwing then you
     * know your file is alive, and in a valid state. In code:
     *
     * using F = basic_path< ... >;
     * F with_path( "file.sgy"_path ); // ok
     * F g;                            // error, F() = delete
     *
     * The problem is that simply disabling the default constructor in the
     * skill means that it cannot be default-initialised, even when using a
     * non-default-constructor. But, since presumably this skill is otherwise
     * empty, it is also an aggregate type, and {}-intialisation works, so
     * simply {}-construct all skills and copy- or move-construct them into our
     * own members.
     *
     * The optimiser should go ham on this and simply elide everything, while
     * still providing the correct static guarantees.
     */
    basic_file( const path& path, const config& cfg = config() ) noexcept(false)
    : Skills< basic_file >( {} ) ... {

        this->consider( path );

        auto mode = this->consider( cfg.mode );

        this->open_path( path, mode );

        this->consider( this->escape() );
        this->consider( this->escape(), cfg );
    }

    /*
     * operator()(Arg) is used for customisation points, but these are internal
     * and should not pollute the basic_path interface. Disallow F(Arg) without
     * an explicit static_cast<Skill&>
     */
    template< typename... A >
    void operator()( A && ... ) = delete;

    template< typename Arg, typename... Args >
    Arg consider( Arg arg, Args && ... args ) {
        apply_all< Skills... >( *this, arg, std::forward< Args >( args ) ... );
        return arg;
    }
};

/*
 * Skills, and their requirements
 */

/*
 * tags for writability and truncability.
 *
 * A truncable file is always writable, but most write-enforcing skills will
 * also enforce non-truncable, to not accidently destroy files.
 *
 * Custom skills that allows or enforce write/trunc behaviour should inherit
 * from these tags, to improve correctness verification of other skills.
 */
template< typename > struct writable {};
template< typename T > struct truncable : writable< T > {};

namespace {
segy_file* segy_open( const path& path, const mode& mode ) noexcept(true) {
    const auto& p = static_cast< const std::string& >(path);
    const auto& m = static_cast< const std::string& >(mode);
    return ::segy_open( p.c_str(), m.c_str() );
}
}

template< typename >
class simple_handle {
    /*
     * The simple_handle is the simplest form of a managed file handle, with
     * copy-opens-new-handle semantics, and completely implements the file
     * handle concept.
     *
     * The file handle concept requires the following (compatible) interface
     *
     * types:
     * ptr_type, a unique_ptr interface compatible type.
     *
     * public methods:
     * ptr_type& get_unique()
     * segy_file* escape()
     *
     * protected methods:
     * void open_path( const path&, const mode& );
     *
     * ptr_type does not have to be exported
     */
public:
    using ptr_type = std::unique_ptr< segy_file, detail::segy_file_deleter >;
    ptr_type&        get_unique()   noexcept(true) { return this->fp; }
    segy_file*       escape()       noexcept(true) { return this->fp.get(); }
    const segy_file* escape() const noexcept(true) { return this->fp.get(); }

    simple_handle()                                  = default;
    simple_handle( simple_handle&& )                 = default;
    simple_handle& operator=( const simple_handle& ) = default;
    simple_handle& operator=( simple_handle&& )      = default;

    simple_handle( const simple_handle& o ) noexcept(false) :
        simple_handle( o.path, o.mode )
    {}

protected:
    simple_handle( const path& path, const mode& mode ) noexcept(false) :
        fp( segy_open( path, mode ) ),
        path( path ),
        mode( mode ) {

        if( this->fp ) return;

        auto m = std::string(mode);
        const std::string allowed_modes[] = {
            "r", "r+", "w+", "rb", "r+b", "w+b",
        };

        /*
         * Allow without 'b', but don't include them in the error message.
         *
         * There are VERY few cases where users should use anything but the
         * named constructors of mode strings, and if they do use literals with
         * trailing 'b', that's also fine (and won't throw), but to fix issues
         * with the strings, one of the r/w/+ should be preferred.
         */
        if( std::find( std::begin( allowed_modes ),
                       std::end( allowed_modes ),
                       m ) == std::end( allowed_modes ) ) {
            const auto msg = "mode must be one of r, r+, w+, was " + m;
            throw std::invalid_argument( msg );
        }

        auto p = std::string(path);
        std::unique_ptr< std::FILE, decltype( &std::fclose ) >
            file( std::fopen( p.c_str(), m.c_str() ), std::fclose );

        if( file ) {
            /* mode isn't garbage, and path apparently is ok too */
            throw std::runtime_error( "unknown failure in segy_open" );
        }

        // file didn't open, so leverage errno to give a better error message
        std::string msg = "unable to open " + p
                        + ": " + std::strerror( errno );
        throw std::runtime_error( msg );
    }

    void open_path( const path& path, const mode& mode ) noexcept(false) {
        *this = simple_handle( path, mode );
    }

private:
    ptr_type fp;
    sio::path path;
    sio::mode mode;
};

template< typename >
struct disable_copy {
    disable_copy( const disable_copy& )             = delete;
    disable_copy& operator=( const disable_copy& )  = delete;

    disable_copy()                                  = default;
    disable_copy( disable_copy&& )                  = default;
    disable_copy& operator=( disable_copy&& )       = default;
};

template< typename Derived >
struct closable {
    void close() noexcept(true) {
        auto* self = static_cast< Derived* >( this );
        self->get_unique().reset( nullptr );
    }
};

template< typename Derived >
struct openable {
    void open( const path& path, const config& cfg = {} ) noexcept(false) {
        static_cast< Derived& >( *this ) = Derived( path, cfg );
    }
};

template< typename Derived >
struct open_status {
    bool is_open() const noexcept(true) {
        return static_cast< const Derived* >( this )->escape();
    }
};

template< typename Derived >
struct readonly {
    void operator()( const mode& out ) noexcept(false) {
        static_assert(
            !any_skills< Derived, writable >::value,
            "read-only file requested, but a skill introduces writability"
        );

        const std::string& str = static_cast< const std::string& >( out );
        const auto write_token_pos = str.find_first_of( "wa+" );

        if( write_token_pos == std::string::npos ) return;

        const std::string msg = str + " enables write (" + str[write_token_pos]
                              + ") in file marked read-only";
        throw std::invalid_argument( msg );

    }
};

template< typename Derived >
struct disable_truncate {
    void operator()( mode& out ) noexcept(false) {
        static_assert(
            !any_skills< Derived, truncable >::value,
            "file marked no-truncable, but skill introduces truncability"
        );

        const auto& str = static_cast< const std::string& >( out );
        if( str.find( 'w' ) != std::string::npos ) {
            constexpr auto msg = "mode with 'w' would truncate, "
                                 "add a truncate-skill to allow";
            throw std::invalid_argument( msg );
        }
    }
};

template< typename Derived >
struct write_always : public writable< Derived >, disable_truncate< Derived > {
    void operator()( mode& out ) noexcept(false) {
        out = mode::readwrite();
    }
};

template< typename Derived >
struct truncate_always : public truncable< Derived > {
    void operator()( mode& out ) noexcept(false) {
        out = mode::truncate();
    }
};

namespace {

std::runtime_error errnomsg( const std::string& msg ) {
    return std::runtime_error(msg + ": " + std::strerror( errno ) );
}

std::runtime_error unknown_error( int errc ) {
    std::string msg = "unhandled error (code " + std::to_string( errc ) + ")";
    return std::runtime_error( msg );
}

}

/*
 * The trace_metadata concept is the basic file metadata. For standard
 * compliant files this is inferred from the binary header.
 *
 * Most skills require a stats concept, because stats provide the basic
 * information used to navigate the file.
 *
 * trace_metadata should provide:
 *
 * int samples()    - samples-per-trace
 * fmt format()     - data format
 * long trace0()    - offset of first trace past extended text headers
 * int tracesize()  - size of each trace in bytes
 * int tracecount() - number of traces in this file
 */

template< typename Derived >
struct trace_meta_fromfile {
    int samples()     const noexcept(true) { return this->smp; }
    sio::fmt format() const noexcept(true) { return this->fmt; }

    long trace0()     const noexcept(true) { return this->tr0;    }
    int  tracesize()  const noexcept(true) { return this->trsize; }
    int  tracecount() const noexcept(true) { return this->traces; }

    void operator()( segy_file* fp ) noexcept(false) {
        char buffer[ SEGY_BINARY_HEADER_SIZE ] = {};
        auto err = segy_binheader( fp, buffer );

        switch( err ) {
            case SEGY_OK: break;

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "unable to seek to binary header" );

            case SEGY_FREAD_ERROR:
                throw errnomsg( "unable to read binary header" );

            default:
                throw unknown_error( err );
        }

        auto samples = segy_samples( buffer );
        auto trace0  = segy_trace0( buffer );
        auto format  = sio::fmt{ segy_format( buffer ) };
        auto trsize  = segy_trsize( int(format), samples );

        /*
         * TODO: move sanity-checking these properties to separate skill? To
         * allow fall-back mechianisms
         */

        if( samples <= 0 )
            throw std::invalid_argument( "expected samples >= 0 (was "
                                       + std::to_string( samples ) + ")" );

        if( trace0 < 0 )
            throw std::invalid_argument( "expected trace0 >= 0 (was "
                                       + std::to_string( trace0 ) + ")" );

        int tracecount;
        err = segy_traces( fp, &tracecount, trace0, trsize );

        switch( err ) {
            case SEGY_OK: break;

            case SEGY_INVALID_ARGS:
                throw std::runtime_error(
                    "first trace position computed after file, "
                    "extended textual header word corrupted "
                    "or file truncated"
                );

            case SEGY_TRACE_SIZE_MISMATCH:
                throw std::runtime_error(
                    "file size does not evenly divide into traces, "
                    "either traces are of uneven length, "
                    "or trace0 is wrong (was " + std::to_string(trace0) + ")"
                );

            default:
                throw unknown_error( err );
        }

        /* all good, so actually change state */
        this->tr0    = trace0;
        this->trsize = trsize;
        this->smp    = samples;
        this->traces = tracecount;
        this->fmt    = sio::fmt{ format };
    }

protected:
    long tr0 = 0;
    int trsize = 0;
    int smp = 0;
    int traces = 0;
    sio::fmt fmt;
};

namespace {

template< typename T, typename OutputIt >
OutputIt copy_n_as( int n, const void* p, OutputIt out ) {
    const auto* typed = reinterpret_cast< const T* >( p );
    return std::copy_n( typed, n, out );
}

}

template< typename Derived >
struct trace_bounds_check {
    void operator()( int i ) const noexcept(false) {
        auto* self = static_cast< const Derived* >( this );
        if( i >= 0 && i < self->tracecount() ) return;

        if( i < 0 ) {
            auto msg = "trace_bounds_check: n (which is "
                     + std::to_string(i) + ") < 0";
            throw std::out_of_range( msg );
        }

        auto msg = "trace_bounds_check: n (which is "
            + std::to_string(i) + ") >= this->tracecount() (which is "
            + std::to_string(self->tracecount()) + ")";
        throw std::out_of_range( msg );
    }
};

template< typename Derived >
struct trace_reader {

    template< typename OutputIt >
    OutputIt get( int i, OutputIt out ) noexcept(false) {
        auto* self = static_cast< Derived* >( this );
        auto* fp = self->escape();

        self->consider( i );
        auto err = segy_readtrace( fp, i,
                                       this->buffer.data(),
                                       self->trace0(),
                                       self->tracesize() );

        switch( err ) {
            case SEGY_OK:
                break;

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "unable to seek trace " + std::to_string(i) );

            case SEGY_FREAD_ERROR:
                throw errnomsg( "unable to read trace " + std::to_string(i) );

            default:
                throw unknown_error( err );
        }

        const auto format = int(self->format());
        const auto samples = self->samples();
        segy_to_native( format, samples, this->buffer.data() );

        const auto* raw = this->buffer.data();
        switch( format ) {
            case SEGY_IBM_FLOAT_4_BYTE:
            case SEGY_IEEE_FLOAT_4_BYTE:
                return copy_n_as< float >( samples, raw, out );

            case SEGY_SIGNED_INTEGER_4_BYTE:
                return copy_n_as< std::int32_t >( samples, raw, out );

            case SEGY_SIGNED_SHORT_2_BYTE:
                return copy_n_as< std::int16_t >( samples, raw, out );

            case SEGY_SIGNED_CHAR_1_BYTE:
                return copy_n_as< std::int8_t >( samples, raw, out );

            default:
                throw std::runtime_error(
                      std::string("this->format is broken (was ")
                    + self->format().description()
                    + ")"
                );
        }
    }

    void operator()( const segy_file* ) noexcept(false) {
        const auto trace_size = static_cast< Derived* >( this )->tracesize();

        if( trace_size == 0 ) {
            const auto msg = "Trace size (in bytes) not computed "
                             "before buffers are resized. "
                             "Move a Stats skill before the trace_reader "
                             "in the skill list";
            throw std::runtime_error( msg );
        }

        this->buffer.resize( trace_size );
    }

protected:
    std::vector< char > buffer;
};

struct trace_header {
    int sequence_line           = 0;
    int sequence_file           = 0;
    int field_record            = 0;
    int traceno_orig            = 0;
    int energy_source_point     = 0;
    int ensemble                = 0;
    int traceno                 = 0;
    int trace_id                = 0;
    int summed_traces           = 0;
    int stacked_traces          = 0;
    int data_use                = 0;
    int offset                  = 0;
    int elevation_receiver      = 0;
    int elevation_source        = 0;
    int depth_source            = 0;
    int datum_receiver          = 0;
    int datum_source            = 0;
    int depth_water_source      = 0;
    int depth_water_group       = 0;
    int elevation_scalar        = 0;
    int coord_scalar            = 0;
    int source_x                = 0;
    int source_y                = 0;
    int group_x                 = 0;
    int group_y                 = 0;
    int coord_units             = 0;
    int weathering_velocity     = 0;
    int subweathering_velocity  = 0;
    int uphole_source           = 0;
    int uphole_group            = 0;
    int static_source           = 0;
    int static_group            = 0;
    int static_total            = 0;
    int lag_a                   = 0;
    int lag_b                   = 0;
    int delay                   = 0;
    int mute_start              = 0;
    int mute_end                = 0;
    int samples                 = 0;
    int sample_interval         = 0;
    int gain_type               = 0;
    int gain_constant           = 0;
    int gain_initial            = 0;
    int correlated              = 0;
    int sweep_freq_start        = 0;
    int sweep_freq_end          = 0;
    int sweep_length            = 0;
    int sweep_type              = 0;
    int sweep_taperlen_start    = 0;
    int sweep_taperlen_end      = 0;
    int taper_type              = 0;
    int alias_filt_freq         = 0;
    int alias_filt_slope        = 0;
    int notch_filt_freq         = 0;
    int notch_filt_slope        = 0;
    int low_cut_freq            = 0;
    int high_cut_freq           = 0;
    int low_cut_slope           = 0;
    int high_cut_slope          = 0;
    int year                    = 0;
    int day                     = 0;
    int hour                    = 0;
    int min                     = 0;
    int sec                     = 0;
    int timecode                = 0;
    int weighting_factor        = 0;
    int geophone_group_roll1    = 0;
    int geophone_group_first    = 0;
    int geophone_group_last     = 0;
    int gap_size                = 0;
    int over_travel             = 0;
    int cdp_x                   = 0;
    int cdp_y                   = 0;
    int iline                   = 0;
    int xline                   = 0;
    int shot_point              = 0;
    int shot_point_scalar       = 0;
    int unit                    = 0;
    int transduction_mantissa   = 0;
    int transduction_exponent   = 0;
    int transduction_unit       = 0;
    int device_id               = 0;
    int scalar_trace_header     = 0;
    int source_type             = 0;
    int source_energy_dir_mant  = 0;
    int source_energy_dir_exp   = 0;
    int source_measure_mant     = 0;
    int source_measure_exp      = 0;
    int source_measure_unit     = 0;
};

template< typename Derived >
struct trace_header_reader {
    trace_header get_th( int i ) noexcept(false) {
        char buffer[ SEGY_TRACE_HEADER_SIZE ] = {};
        auto* self = static_cast< Derived* >( this );

        self->consider( i );
        auto err = segy_traceheader( self->escape(), i,
                                                     buffer,
                                                     self->trace0(),
                                                     self->tracesize() );

        switch( err ) {
            case SEGY_OK: break;

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "unable to seek trace " + std::to_string(i) );

            case SEGY_FREAD_ERROR:
                throw errnomsg( "unable to read trace " + std::to_string(i) );

            default:
            throw unknown_error( err );
        }

        const auto getf = [&]( int key ) {
            int32_t f;
            segy_get_field( buffer, key, &f );
            return f;
        };

        trace_header h;
        h.sequence_line          = getf( SEGY_TR_SEQ_LINE );
        h.sequence_file          = getf( SEGY_TR_SEQ_FILE );
        h.field_record           = getf( SEGY_TR_FIELD_RECORD );
        h.traceno_orig           = getf( SEGY_TR_NUMBER_ORIG_FIELD );
        h.energy_source_point    = getf( SEGY_TR_ENERGY_SOURCE_POINT );
        h.ensemble               = getf( SEGY_TR_ENSEMBLE );
        h.traceno                = getf( SEGY_TR_NUM_IN_ENSEMBLE );
        h.trace_id               = getf( SEGY_TR_TRACE_ID );
        h.summed_traces          = getf( SEGY_TR_SUMMED_TRACES );
        h.stacked_traces         = getf( SEGY_TR_STACKED_TRACES );
        h.data_use               = getf( SEGY_TR_DATA_USE );
        h.offset                 = getf( SEGY_TR_OFFSET );
        h.elevation_receiver     = getf( SEGY_TR_RECV_GROUP_ELEV );
        h.elevation_source       = getf( SEGY_TR_SOURCE_SURF_ELEV );
        h.depth_source           = getf( SEGY_TR_SOURCE_DEPTH );
        h.datum_receiver         = getf( SEGY_TR_RECV_DATUM_ELEV );
        h.datum_source           = getf( SEGY_TR_SOURCE_DATUM_ELEV );
        h.depth_water_source     = getf( SEGY_TR_SOURCE_WATER_DEPTH );
        h.depth_water_group      = getf( SEGY_TR_GROUP_WATER_DEPTH );
        h.elevation_scalar       = getf( SEGY_TR_ELEV_SCALAR );
        h.coord_scalar           = getf( SEGY_TR_SOURCE_GROUP_SCALAR );
        h.source_x               = getf( SEGY_TR_SOURCE_X );
        h.source_y               = getf( SEGY_TR_SOURCE_Y );
        h.group_x                = getf( SEGY_TR_GROUP_X );
        h.group_y                = getf( SEGY_TR_GROUP_Y );
        h.coord_units            = getf( SEGY_TR_COORD_UNITS );
        h.weathering_velocity    = getf( SEGY_TR_WEATHERING_VELO );
        h.subweathering_velocity = getf( SEGY_TR_SUBWEATHERING_VELO );
        h.uphole_source          = getf( SEGY_TR_SOURCE_UPHOLE_TIME );
        h.uphole_group           = getf( SEGY_TR_GROUP_UPHOLE_TIME );
        h.static_source          = getf( SEGY_TR_SOURCE_STATIC_CORR );
        h.static_group           = getf( SEGY_TR_GROUP_STATIC_CORR );
        h.static_total           = getf( SEGY_TR_TOT_STATIC_APPLIED );
        h.lag_a                  = getf( SEGY_TR_LAG_A );
        h.lag_b                  = getf( SEGY_TR_LAG_B );
        h.delay                  = getf( SEGY_TR_DELAY_REC_TIME );
        h.mute_start             = getf( SEGY_TR_MUTE_TIME_START );
        h.mute_end               = getf( SEGY_TR_MUTE_TIME_END );
        h.samples                = getf( SEGY_TR_SAMPLE_COUNT );
        h.sample_interval        = getf( SEGY_TR_SAMPLE_INTER );
        h.gain_type              = getf( SEGY_TR_GAIN_TYPE );
        h.gain_constant          = getf( SEGY_TR_INSTR_GAIN_CONST );
        h.gain_initial           = getf( SEGY_TR_INSTR_INIT_GAIN );
        h.correlated             = getf( SEGY_TR_CORRELATED );
        h.sweep_freq_start       = getf( SEGY_TR_SWEEP_FREQ_START );
        h.sweep_freq_end         = getf( SEGY_TR_SWEEP_FREQ_END );
        h.sweep_length           = getf( SEGY_TR_SWEEP_LENGTH );
        h.sweep_type             = getf( SEGY_TR_SWEEP_TYPE );
        h.sweep_taperlen_start   = getf( SEGY_TR_SWEEP_TAPERLEN_START );
        h.sweep_taperlen_end     = getf( SEGY_TR_SWEEP_TAPERLEN_END );
        h.taper_type             = getf( SEGY_TR_TAPER_TYPE );
        h.alias_filt_freq        = getf( SEGY_TR_ALIAS_FILT_FREQ );
        h.alias_filt_slope       = getf( SEGY_TR_ALIAS_FILT_SLOPE );
        h.notch_filt_freq        = getf( SEGY_TR_NOTCH_FILT_FREQ );
        h.notch_filt_slope       = getf( SEGY_TR_NOTCH_FILT_SLOPE );
        h.low_cut_freq           = getf( SEGY_TR_LOW_CUT_FREQ );
        h.high_cut_freq          = getf( SEGY_TR_HIGH_CUT_FREQ );
        h.low_cut_slope          = getf( SEGY_TR_LOW_CUT_SLOPE );
        h.high_cut_slope         = getf( SEGY_TR_HIGH_CUT_SLOPE );
        h.year                   = getf( SEGY_TR_YEAR_DATA_REC );
        h.day                    = getf( SEGY_TR_DAY_OF_YEAR );
        h.hour                   = getf( SEGY_TR_HOUR_OF_DAY );
        h.min                    = getf( SEGY_TR_MIN_OF_HOUR );
        h.sec                    = getf( SEGY_TR_SEC_OF_MIN );
        h.timecode               = getf( SEGY_TR_TIME_BASE_CODE );
        h.weighting_factor       = getf( SEGY_TR_WEIGHTING_FAC );
        h.geophone_group_roll1   = getf( SEGY_TR_GEOPHONE_GROUP_ROLL1 );
        h.geophone_group_first   = getf( SEGY_TR_GEOPHONE_GROUP_FIRST );
        h.geophone_group_last    = getf( SEGY_TR_GEOPHONE_GROUP_LAST );
        h.gap_size               = getf( SEGY_TR_GAP_SIZE );
        h.over_travel            = getf( SEGY_TR_OVER_TRAVEL );
        h.cdp_x                  = getf( SEGY_TR_CDP_X );
        h.cdp_y                  = getf( SEGY_TR_CDP_Y );
        h.iline                  = getf( SEGY_TR_INLINE );
        h.xline                  = getf( SEGY_TR_CROSSLINE );
        h.shot_point             = getf( SEGY_TR_SHOT_POINT );
        h.shot_point_scalar      = getf( SEGY_TR_SHOT_POINT_SCALAR );
        h.unit                   = getf( SEGY_TR_MEASURE_UNIT );
        h.transduction_mantissa  = getf( SEGY_TR_TRANSDUCTION_MANT );
        h.transduction_exponent  = getf( SEGY_TR_TRANSDUCTION_EXP );
        h.transduction_unit      = getf( SEGY_TR_TRANSDUCTION_UNIT );
        h.device_id              = getf( SEGY_TR_DEVICE_ID );
        h.scalar_trace_header    = getf( SEGY_TR_SCALAR_TRACE_HEADER );
        h.source_type            = getf( SEGY_TR_SOURCE_TYPE );
        h.source_energy_dir_mant = getf( SEGY_TR_SOURCE_ENERGY_DIR_MANT );
        h.source_energy_dir_exp  = getf( SEGY_TR_SOURCE_ENERGY_DIR_EXP );
        h.source_measure_mant    = getf( SEGY_TR_SOURCE_MEASURE_MANT );
        h.source_measure_exp     = getf( SEGY_TR_SOURCE_MEASURE_EXP );
        h.source_measure_unit    = getf( SEGY_TR_SOURCE_MEASURE_UNIT );

        return h;
    }
};

template< typename Derived >
struct trace_writer : public trace_reader< Derived > {
    using trace_reader< Derived >::trace_reader;

    template< typename InputIt >
    InputIt put( int i, InputIt in ) {

        auto* self = static_cast< Derived* >( this );
        auto* fp = self->escape();

        self->consider( i );

        static_assert(
            self->template know_any< writable, write_always >(),
            "trace_writer needs a 'writable' skill"
        );

        auto* raw = this->buffer.data();
        const auto len = self->samples();
        const auto format = int(self->format());
        switch( format ) {
            case SEGY_IBM_FLOAT_4_BYTE:
            case SEGY_IEEE_FLOAT_4_BYTE:
                std::copy_n( in, len, reinterpret_cast< float* >( raw ) );
                break;

            case SEGY_SIGNED_INTEGER_4_BYTE:
                std::copy_n( in, len, reinterpret_cast< std::int32_t* >( raw ) );
                break;

            case SEGY_SIGNED_SHORT_2_BYTE:
                std::copy_n( in, len, reinterpret_cast< std::int16_t* >( raw ) );
                break;

            case SEGY_SIGNED_CHAR_1_BYTE:
                std::copy_n( in, len, reinterpret_cast< std::int8_t* >( raw ) );

                break;

            default:
                throw std::runtime_error(
                      std::string("this->format is broken (was ")
                    + self->format().description()
                    + ")"
                );
        }

        segy_from_native( format, len, this->buffer.data() );
        auto err = segy_writetrace( fp,
                                    i,
                                    this->buffer.data(),
                                    self->trace0(),
                                    self->tracesize() );

        switch( err ) {
            case SEGY_OK:
                break;

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "unable to seek trace " + std::to_string(i) );

            case SEGY_FWRITE_ERROR:
                throw errnomsg( "unable to write trace " + std::to_string(i) );

            default:
                throw unknown_error( err );
        }

        return in + len;
    }
};

enum class line_direction { in, cross };

template< typename Derived >
struct cube_stats {

    sio::sorting sorting() const noexcept(true) { return this->sort;   }
    int inlinecount()      const noexcept(true) { return this->ilines; }
    int crosslinecount()   const noexcept(true) { return this->xlines; }
    int offsetcount()      const noexcept(true) { return this->offs;   }

    void operator()( segy_file* fp, const config& cfg ) {
        auto* self = static_cast< Derived* >( this );

        const auto il = int(cfg.iline);
        const auto xl = int(cfg.xline);

        int sort = SEGY_UNKNOWN_SORTING;

        int err;
        err = segy_sorting( fp,
                            il,
                            xl,
                            SEGY_TR_OFFSET,
                            &sort,
                            self->trace0(),
                            self->tracesize() );

        switch( err ) {
            case SEGY_OK: break;

            case SEGY_INVALID_FIELD:
                // TODO: figure out which one
                throw std::invalid_argument( "invalid il/xl/offset field" );

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "seek error while determining sorting" );

            case SEGY_FREAD_ERROR:
                throw errnomsg( "read error while determining sorting" );

            case SEGY_INVALID_SORTING:
                throw std::invalid_argument( "file is not sorted" );

            default:
                throw unknown_error( err );
        }

        const auto srt = sio::sorting{ sort };

        int ils, xls, ofs;
        err = segy_offsets( fp,
                            il,
                            xl,
                            self->tracecount(),
                            &ofs,
                            self->trace0(),
                            self->tracesize() );

        switch( err ) {
            case SEGY_OK: break;

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "seek error while counting offsets" );

            case SEGY_FREAD_ERROR:
                throw errnomsg( "read error while counting offsets" );

            default:
                throw unknown_error( err );
        }

        err = segy_lines_count( fp,
                                il,
                                xl,
                                sort,
                                ofs,
                                &ils,
                                &xls,
                                self->trace0(),
                                self->tracesize() );

        switch( err ) {
            case SEGY_OK: break;

            case SEGY_NOTFOUND:
                throw std::invalid_argument( "found only offsets in file" );

            case SEGY_FSEEK_ERROR:
                throw errnomsg( "seek error while counting lines" );

            case SEGY_FREAD_ERROR:
                throw errnomsg( "read error while counting lines" );

            default:
                throw unknown_error( err );
        }

        this->sort   = srt;
        this->ilines = ils;
        this->xlines = xls;
        this->offs   = ofs;
    }

private:
    sio::sorting sort;
    int ilines;
    int xlines;
    int offs;
};

template< typename >
struct disable_default {
    disable_default() = delete;
};


template< template< typename > typename ... Extras >
using basic_unstructured = basic_file< simple_handle,
                                       trace_meta_fromfile,
                                       trace_reader,
                                       disable_truncate,
                                       Extras... >;

using unstructured          = basic_unstructured<>;
using unstructured_readonly = basic_unstructured< readonly >;

}

#endif //SEGYIO_HPP
