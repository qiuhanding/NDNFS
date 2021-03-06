/*
 * Copyright (c) 2013 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Wentao Shang <wentao@cs.ucla.edu>
 */

#include "ndnfs.h"
#include "directory.h"
#include "file.h"
#include "attribute.h"


//#include <ndn.cxx.h>
#include <unistd.h>
#include <sys/types.h>

using namespace std;
using namespace boost;
using namespace mongo;
//using namespace ndn;

//ndn::Wrapper ndn_wrapper;
const char *db_name = "ndnfs.root";
ndn::Name signer("/ndn/ucla.edu/qiuhan");

string ndnfs::global_prefix;

ndn::Ptr<ndn::security::OSXPrivatekeyStore> privateStoragePtr = ndn::Ptr<ndn::security::OSXPrivatekeyStore>::Create();
ndn::Ptr<ndn::security::Keychain> keychain = ndn::Ptr<ndn::security::Keychain>(new ndn::security::Keychain(privateStoragePtr, "/Users/ndn/qiuhan/policy", "/tmp/encryption.db"));//////policy needs to be changed

const int ndnfs::dir_type = 0;
const int ndnfs::file_type = 1;
const int ndnfs::version_type = 2;
const int ndnfs::segment_type = 3;

const int ndnfs::seg_size = 4096;  // size of the content in each content object segment counted in bytes
const int ndnfs::seg_size_shift = 12;

int ndnfs::user_id = 0;
int ndnfs::group_id = 0;

static void create_fuse_operations(struct fuse_operations *fuse_op)
{
    fuse_op->getattr  = ndnfs_getattr;
    fuse_op->chmod    = ndnfs_chmod;
    fuse_op->setxattr = ndnfs_setxattr;
    fuse_op->open     = ndnfs_open;
    fuse_op->read     = ndnfs_read;
    fuse_op->readdir  = ndnfs_readdir;
    fuse_op->create   = ndnfs_create;
    fuse_op->write    = ndnfs_write;
    fuse_op->truncate = ndnfs_truncate;
    fuse_op->release  = ndnfs_release;
    fuse_op->unlink   = ndnfs_unlink;
    fuse_op->mkdir    = ndnfs_mkdir;
    fuse_op->rmdir    = ndnfs_rmdir;
}

static struct fuse_operations ndnfs_fs_ops;

struct ndnfs_config {
    char *prefix;
};

#define NDNFS_OPT(t, p, v) { t, offsetof(struct ndnfs_config, p), v }

static struct fuse_opt ndnfs_opts[] = {
    NDNFS_OPT("prefix=%s", prefix, 0),
    FUSE_OPT_END
};

int main(int argc, char **argv)
{
    assert((1 << ndnfs::seg_size_shift) == ndnfs::seg_size);
    ndnfs::global_prefix = "/ndn/ucla.edu/qiuhan/dummy/ndnfs";
    
    cout << "main: NDNFS version beta 0.1" << endl;
    
    // uid and gid will be set to that of the user who starts the fuse process
    ndnfs::user_id = getuid();
    ndnfs::group_id = getgid();

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct ndnfs_config conf;
    memset(&conf, 0, sizeof(conf));
    fuse_opt_parse(&args, &conf, ndnfs_opts, NULL);

    if (conf.prefix != NULL) {
	int plen = strlen(conf.prefix);
	if (plen > 1 && conf.prefix[plen - 1] == '/')
	    conf.prefix[plen - 1] = 0;  // Remove the last '/' in the prefix is there is any
	ndnfs::global_prefix = string(conf.prefix);
    }
    cout << "main: global prefix is " << ndnfs::global_prefix << endl;

    cout << "main: test mongodb connection..." << endl;
    
    ScopedDbConnection *c = ScopedDbConnection::getScopedDbConnection("localhost");
    if (c->ok()) {
        cout << "main: ok" << endl;
    } else {
        cout << "main: cannot connect to local mongodb, quit..." << endl;
        c->done();
        delete c;
        return -1;
    }
    
    cout << "main: mount root directory..." << endl;
    // For test purpose, clear database first
//    c->conn().dropCollection(db_name);
//    c->conn().createCollection(db_name);
    
    // Check database first
    auto_ptr<DBClientCursor> cursor = c->conn().query(db_name, QUERY("_id" << "/"));
    if (!cursor->more()) {
        // Create root directory as an empty folder if database is empty
	int now = time(0);
	BSONObj root_dir = BSONObjBuilder().append("_id", "/").append("type", ndnfs::dir_type).append("mode", 0777)
	    .append("atime", now).append("mtime", now).append("data", BSONArrayBuilder().arr()).obj();
        c->conn().insert(db_name, root_dir);
    }
    cout << "main: ok" << endl;
    
    c->done();
    delete c;
    
    create_fuse_operations(&ndnfs_fs_ops);
    
    cout << "main: enter FUSE main loop" << endl << endl;
    return fuse_main(args.argc, args.argv, &ndnfs_fs_ops, NULL);
}
