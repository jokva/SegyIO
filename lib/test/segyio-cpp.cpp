#include <catch/catch.hpp>

#include <segyio/segyio.hpp>

using namespace sio;
using namespace sio::literals;

TEST_CASE( "verify strong string typedef same as string", "[c++]" ) {
    auto lhs = "lhs"_path;
    auto rhs = "rhs"_path;
    std::string strlhs = "lhs";
    std::string strrhs = "rhs";

    auto str_noexcept  = noexcept( std::swap( strlhs, strrhs ) );
    auto path_noexcept = noexcept( std::swap( lhs, rhs ) );
    CHECK( str_noexcept == path_noexcept );
}

TEST_CASE( "verify strong int typedef swap noexcept", "[c++]" ) {
    sio::ilbyte lhs;
    sio::ilbyte rhs;
    auto is_noexcept = noexcept( std::swap( lhs, rhs ) );
    CHECK( is_noexcept );
}

TEST_CASE( "basic non-copyable can open file", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::disable_copy >;
    F f( "test-data/small.sgy"_path );
}

TEST_CASE( "basic copyable can open file", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle >;
    F f( sio::path{ "test-data/small.sgy" } );
}

TEST_CASE( "basic copyable file is copyable and moveable", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle >;
    F f( sio::path{ "test-data/small.sgy" } );
    auto g = f;

    auto copyable = std::is_copy_constructible< F >::value;
    auto movable = std::is_move_constructible< F >::value;

    CHECK( copyable );
    CHECK( movable );
}

TEST_CASE( "basic non-copyable file is non-copyable and movable", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::disable_copy >;
    auto copyable = std::is_copy_constructible< F >::value;
    auto movable  = std::is_move_constructible< F >::value;

    CHECK_FALSE( copyable );
    CHECK( movable );
}

TEST_CASE( "file open/closed open_status is queryable", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::open_status >;

    F f( sio::path{ "test-data/small.sgy" } );
    CHECK( f.is_open() );
}

TEST_CASE( "file is closable", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::closable, sio::open_status >;

    F f( sio::path{ "test-data/small.sgy" } );
    f.close();
    CHECK( !f.is_open() );
}

TEST_CASE( "a moved-assigned-from file is closed", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::disable_copy, sio::open_status >;
    F src( sio::path{ "test-data/small.sgy" } );
    auto dst = std::move( src );
    CHECK( !src.is_open() );
    CHECK(  dst.is_open() );
}

TEST_CASE( "a moved-constructed-from file is closed", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::disable_copy, sio::open_status >;
    F src( sio::path{ "test-data/small.sgy" } );
    F dst( std::move( src ) );
    CHECK( !src.is_open() );
    CHECK(  dst.is_open() );
}

TEST_CASE( "throws on non-existing paths", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle >;
    CHECK_THROWS( F( sio::path{ "garbage" } ) );
}

TEST_CASE( "open can be deferred", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::open_status,
                               sio::openable >;
    F f;
    REQUIRE( !f.is_open() );
    f.open( sio::path{ "test-data/small.sgy" } );
    CHECK( f.is_open() );
}

TEST_CASE( "copying-and-closing leaves other intact", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::open_status, sio::closable >;
    F f( sio::path{ "test-data/small.sgy" } );
    auto g = f;

    REQUIRE( f.is_open() );
    REQUIRE( g.is_open() );

    SECTION( "closing copy leaves original intact" ) {
        g.close();
        CHECK(  f.is_open() );
        CHECK( !g.is_open() );
    }

    SECTION( "closing original leaves copy intact" ) {
        f.close();
        CHECK(  g.is_open() );
        CHECK( !f.is_open() );
    }
}

TEST_CASE( "file must be open-write_always", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::write_always >;
    F f( sio::path{ "test-data/small.sgy" } );
}

TEST_CASE( "file-stat", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle, sio::trace_meta_fromfile >;
    F f( sio::path{ "test-data/small.sgy" } );
}

TEST_CASE( "file-non-defualt-ctor", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::trace_meta_fromfile,
                               sio::trace_reader,
                               sio::disable_default >;
    F f( sio::path{ "test-data/small.sgy" } );
    const auto default_ctor = std::is_default_constructible< F >::value;
    CHECK_FALSE( default_ctor );
}

TEST_CASE( "array-get only", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::trace_meta_fromfile,
                               sio::trace_reader >;

    F f( sio::path{ "test-data/small.sgy" } );

    std::vector< float > out;
    f.get( 0, std::back_inserter( out ) );
}

TEST_CASE( "array-get bounds check", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::trace_meta_fromfile,
                               sio::trace_reader,
                               sio::trace_bounds_check >;

    F f( sio::path{ "test-data/small.sgy" } );

    std::vector< float > out;
    CHECK_THROWS_AS( f.get( 1000, std::back_inserter( out ) ),
                     std::out_of_range );
}

TEST_CASE( "get trace-header", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::trace_meta_fromfile,
                               sio::trace_header_reader >;

    F f( sio::path{ "test-data/small.sgy" } );

    auto x = f.get_th( 0 );
    auto y = f.get_th( 1 );
    auto z = f.get_th( 5 );

    CHECK( x.iline == 1 );
    CHECK( y.iline == 1 );
    CHECK( z.iline == 2 );

    CHECK( x.xline == 20 );
    CHECK( y.xline == 21 );
    CHECK( z.xline == 20 );
}

TEST_CASE( "array-put only", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::write_always,
                               sio::trace_meta_fromfile,
                               sio::trace_writer >;

    F f( sio::path{ "small.sgy" } );

    std::vector< float > in( 50 );
    for( std::size_t i = 0; i < in.size(); ++i ) in[i] = i;
    f.put( 0, in.begin() );

    std::vector< float > out;
    f.get( 0, std::back_inserter( out ) );
}

TEST_CASE( "cube-stats", "[c++]" ) {
    using F = sio::basic_file< sio::simple_handle,
                               sio::trace_meta_fromfile,
                               sio::cube_stats >;

    F f( sio::path{ "test-data/small.sgy" } );

    CHECK( f.inlinecount() == 5 );
    CHECK( f.crosslinecount() == 5 );
}
