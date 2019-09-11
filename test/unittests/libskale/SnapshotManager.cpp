#include <libskale/SnapshotManager.h>
#include <skutils/btrfs.h>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <string>

#include <stdlib.h>

#include <sys/stat.h>

using namespace std;
namespace fs = boost::filesystem;

static void check_sudo() {
    char* id_str = getenv( "SUDO_UID" );
    if ( id_str == NULL ) {
        cerr << "Please run under sudo" << endl;
        exit( -1 );
    }

    int id;
    sscanf( id_str, "%d", &id );

    uid_t ru, eu, su;
    getresuid( &ru, &eu, &su );
    cerr << ru << " " << eu << " " << su << endl;

    if ( geteuid() != 0 ) {
        cerr << "Need to be root" << endl;
        exit( -1 );
    }
}

struct FixtureCommon {
    const string BTRFS_FILE_PATH = "btrfs.file";
    const string BTRFS_DIR_PATH = "btrfs";

    void dropRoot() {
        char* id_str = getenv( "SUDO_UID" );
        int id;
        sscanf( id_str, "%d", &id );
        seteuid( id );
    }

    void gainRoot() { seteuid( 0 ); }
};

struct BtrfsFixture : public FixtureCommon {
    BtrfsFixture() {
        check_sudo();

        dropRoot();

        system( ( "dd if=/dev/zero of=" + BTRFS_FILE_PATH + " bs=1M count=200" ).c_str() );
        system( ( "mkfs.btrfs " + BTRFS_FILE_PATH ).c_str() );
        system( ( "mkdir " + BTRFS_DIR_PATH ).c_str() );

        gainRoot();
        system( ( "mount " + BTRFS_FILE_PATH + " " + BTRFS_DIR_PATH ).c_str() );
        dropRoot();

        btrfs.subvolume.create( ( BTRFS_DIR_PATH + "/vol1" ).c_str() );
        btrfs.subvolume.create( ( BTRFS_DIR_PATH + "/vol2" ).c_str() );
        // system( ( "mkdir " + BTRFS_DIR_PATH + "/snapshots" ).c_str() );

        gainRoot();
    }

    ~BtrfsFixture() {
        gainRoot();
        system( ( "umount " + BTRFS_DIR_PATH ).c_str() );
        system( ( "rmdir " + BTRFS_DIR_PATH ).c_str() );
        system( ( "rm " + BTRFS_FILE_PATH ).c_str() );
    }
};

struct NoBtrfsFixture : public FixtureCommon {
    NoBtrfsFixture() {
        dropRoot();
        system( ( "mkdir " + BTRFS_DIR_PATH ).c_str() );
        system( ( "mkdir " + BTRFS_DIR_PATH + "/vol1" ).c_str() );
        system( ( "mkdir " + BTRFS_DIR_PATH + "/vol2" ).c_str() );
        gainRoot();
    }
    ~NoBtrfsFixture() {
        gainRoot();
        system( ( "rm -rf " + BTRFS_DIR_PATH ).c_str() );
    }
};

BOOST_AUTO_TEST_SUITE( BtrfsTestSuite )

BOOST_FIXTURE_TEST_CASE( SimplePositiveTest, BtrfsFixture ) {
    SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "vol2"} );

    // add files 1
    fs::create_directory( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d11" );
    fs::create_directory( fs::path( BTRFS_DIR_PATH ) / "vol2" / "d21" );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d11" ) );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol2" / "d21" ) );

    // create snapshot 1 and check its presense
    mgr.doSnapshot( 1 );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "1" / "vol1" / "d11" ) );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "1" / "vol2" / "d21" ) );

    // add and remove something
    fs::create_directory( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d12" );
    fs::remove( fs::path( BTRFS_DIR_PATH ) / "vol2" / "d21" );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d12" ) );
    BOOST_REQUIRE( !fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol2" / "d21" ) );

    // create snapshot 2 and check files 1 and files 2
    mgr.doSnapshot( 2 );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol1" / "d11" ) );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol1" / "d12" ) );
    BOOST_REQUIRE( !fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol2" / "d21" ) );

    // check that files appear/disappear on restore
    mgr.restoreSnapshot( 1 );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d11" ) );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol2" / "d21" ) );
    BOOST_REQUIRE( !fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d12" ) );

    fs::path diff12 = mgr.makeDiff( 1, 2 );
    btrfs.subvolume._delete( ( BTRFS_DIR_PATH + "/snapshots/2/vol1" ).c_str() );
    btrfs.subvolume._delete( ( BTRFS_DIR_PATH + "/snapshots/2/vol2" ).c_str() );
    fs::remove_all( BTRFS_DIR_PATH + "/snapshots/2" );
    BOOST_REQUIRE( !fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" ) );

    mgr.importDiff( 2, diff12 );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol1" / "d11" ) );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol1" / "d12" ) );
    BOOST_REQUIRE( !fs::exists( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol2" / "d21" ) );

    mgr.restoreSnapshot( 2 );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d11" ) );
    BOOST_REQUIRE( fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol1" / "d12" ) );
    BOOST_REQUIRE( !fs::exists( fs::path( BTRFS_DIR_PATH ) / "vol2" / "d21" ) );
}

BOOST_FIXTURE_TEST_CASE( NoBtrfsTest, NoBtrfsFixture ) {
    BOOST_REQUIRE_THROW( SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "vol2"} ),
        SnapshotManager::CannotPerformBtrfsOperation );
}

BOOST_FIXTURE_TEST_CASE( NonRootTest, BtrfsFixture ) {
    dropRoot();
    BOOST_REQUIRE_THROW( SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "vol2"} ),
        SnapshotManager::CannotCreate );
}

BOOST_FIXTURE_TEST_CASE( BadPathTest, BtrfsFixture ) {
    BOOST_REQUIRE_EXCEPTION(
        SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ) / "_invalid", {"vol1", "vol2"} ),
        SnapshotManager::InvalidPath, [this]( const SnapshotManager::InvalidPath& ex ) -> bool {
            return ex.path == fs::path( BTRFS_DIR_PATH ) / "_invalid";
        } );

    BOOST_REQUIRE_EXCEPTION(
        SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "invalid3", "vol2"} ),
        SnapshotManager::InvalidPath, [this]( const SnapshotManager::InvalidPath& ex ) -> bool {
            return ex.path == fs::path( BTRFS_DIR_PATH ) / "invalid3";
        } );
}

BOOST_FIXTURE_TEST_CASE( SnapshotTest, BtrfsFixture ) {
    SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "vol2"} );

    BOOST_REQUIRE_NO_THROW( mgr.doSnapshot( 2 ) );
    BOOST_REQUIRE_THROW( mgr.doSnapshot( 2 ), SnapshotManager::SnapshotPresent );

    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" ).c_str(), 0 );

    dropRoot();

    BOOST_REQUIRE_EXCEPTION( mgr.doSnapshot( 3 ), SnapshotManager::CannotRead,
        [this]( const SnapshotManager::CannotRead& ex ) -> bool {
            return ex.path == fs::path( BTRFS_DIR_PATH ) / "snapshots" / "3";
        } );

    gainRoot();
    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" ).c_str(), 0111 );
    dropRoot();

    BOOST_REQUIRE_EXCEPTION( mgr.doSnapshot( 3 ), SnapshotManager::CannotCreate,
        [this]( const SnapshotManager::CannotCreate& ex ) -> bool {
            return ex.path == fs::path( BTRFS_DIR_PATH ) / "snapshots" / "3";
        } );

    // interesting that under normal user we still can do snapshot
    BOOST_REQUIRE_NO_THROW( mgr.restoreSnapshot( 2 ) );
}

BOOST_FIXTURE_TEST_CASE( RestoreTest, BtrfsFixture ) {
    SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "vol2"} );
    BOOST_REQUIRE_THROW( mgr.restoreSnapshot( 2 ), SnapshotManager::SnapshotAbsent );

    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" ).c_str(), 0 );
    dropRoot();

    BOOST_REQUIRE_EXCEPTION( mgr.restoreSnapshot( 2 ), SnapshotManager::CannotRead,
        [this]( const SnapshotManager::CannotRead& ex ) -> bool {
            return ex.path == fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2";
        } );

    gainRoot();
    BOOST_REQUIRE_NO_THROW( mgr.doSnapshot( 2 ) );
    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" ).c_str(), 0111 );
    chmod( ( fs::path( BTRFS_DIR_PATH ) ).c_str(), 0111 );
    dropRoot();

    BOOST_REQUIRE_NO_THROW( mgr.restoreSnapshot( 2 ) );

    BOOST_REQUIRE_EQUAL(
        0, btrfs.subvolume._delete(
               ( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2" / "vol1" ).c_str() ) );
    BOOST_REQUIRE_THROW( mgr.restoreSnapshot( 2 ), SnapshotManager::CannotPerformBtrfsOperation );
}

BOOST_FIXTURE_TEST_CASE( DiffTest, BtrfsFixture ) {
    SnapshotManager mgr( fs::path( BTRFS_DIR_PATH ), {"vol1", "vol2"} );
    mgr.doSnapshot( 2 );
    fs::create_directory( fs::path( BTRFS_DIR_PATH ) / "vol1" / "dir" );
    mgr.doSnapshot( 4 );

    BOOST_REQUIRE_THROW( mgr.makeDiff( 1, 3 ), SnapshotManager::SnapshotAbsent );
    BOOST_REQUIRE_THROW( mgr.makeDiff( 2, 3 ), SnapshotManager::SnapshotAbsent );
    BOOST_REQUIRE_THROW( mgr.makeDiff( 1, 2 ), SnapshotManager::SnapshotAbsent );

    dropRoot();

    fs::path tmp;
    BOOST_REQUIRE_NO_THROW( tmp = mgr.makeDiff( 2, 4 ) );
    fs::remove( tmp );

    BOOST_REQUIRE_NO_THROW( tmp = mgr.makeDiff( 2, 2 ) );
    fs::remove( tmp );

    BOOST_REQUIRE_NO_THROW( tmp = mgr.makeDiff( 4, 2 ) );
    fs::remove( tmp );

    gainRoot();
    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" ).c_str(), 0 );
    dropRoot();

    BOOST_REQUIRE_EXCEPTION( tmp = mgr.makeDiff( 2, 4 ), SnapshotManager::CannotRead,
        [this]( const SnapshotManager::CannotRead& ex ) -> bool {
            return ex.path == fs::path( BTRFS_DIR_PATH ) / "snapshots" / "2";
        } );

    gainRoot();
    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" ).c_str(), 111 );
    chmod( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "4" / "vol1" ).c_str(), 0 );
    dropRoot();

    // strange - but ok...
    BOOST_REQUIRE_NO_THROW( tmp = mgr.makeDiff( 2, 4 ) );
    BOOST_REQUIRE_GT( fs::file_size( tmp ), 0 );
    fs::remove( tmp );

    btrfs.subvolume._delete( ( fs::path( BTRFS_DIR_PATH ) / "snapshots" / "4" / "vol1" ).c_str() );

    BOOST_REQUIRE_THROW( tmp = mgr.makeDiff( 2, 4 ), SnapshotManager::CannotPerformBtrfsOperation );
}

BOOST_AUTO_TEST_SUITE_END()