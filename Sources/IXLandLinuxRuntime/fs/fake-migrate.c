#import <IXLandLinuxRuntime/fs/fake-db.h>
#import <IXLandLinuxRuntime/fs/sqlutil.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/fs.h>

#import <IXLandLinuxRuntime/util/debug.h>

// The value of the user_version pragma is used to decide what needs migrating.

static struct migration {
    const char *sql;
    void (*migrate)(struct fakefs_db *fs);
} migrations[] = {
    // version 1: add another index
    { .sql = "create index inode_to_path on paths (inode, path);", .migrate = NULL },
    // version 2: add foreign key constraint on paths, create trigger to automatically cleanup stats
    { .sql =
          "create table paths_new (path blob primary key, inode integer references stats(inode));"
          "insert into paths_new select * from paths where exists (select 1 from stats where inode "
          "= "
          "paths.inode);"
          "drop table paths; alter table paths_new rename to paths;"
          "create index inode_to_path on paths (inode, path);"
          "delete from stats where not exists (select 1 from paths where inode = stats.inode);"
          "create trigger delete_path after delete on paths "
          "when not exists (select 1 from paths where inode = old.inode) "
          "begin "
          "delete from stats where not exists (select 1 from paths where inode = old.inode) and "
          "inode "
          "= old.inode; "
          "end;",
      .migrate = NULL },
    // version 3: the trigger was a mistake
    { .sql = "drop trigger delete_path", .migrate = NULL },
};

int fakefs_migrate(struct fakefs_db *fs, int UNUSED(root_fd))
{
    sqlite3 *db = fs->db;
    int err;
    sqlite3_stmt *user_version = PREPARE("pragma user_version");
    STEP(user_version);
    int version = sqlite3_column_int(user_version, 0);
    FINALIZE(user_version);

    EXEC("begin");
    int versions = sizeof(migrations) / sizeof(migrations[0]);
    while (version < versions) {
        struct migration m = migrations[version];
        if (m.sql != NULL)
            EXEC(m.sql);
        if (m.migrate != NULL)
            m.migrate(fs);
        version++;
    }
    // for some reason placeholders aren't allowed in pragmas
    char *pragma_user_version = sqlite3_mprintf("pragma user_version = %d", version);
    EXEC(pragma_user_version);
    sqlite3_free(pragma_user_version);
    EXEC("commit");

    return 0;
}
