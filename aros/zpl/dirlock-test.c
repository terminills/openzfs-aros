/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <pthread.h>
#include <proto/exec.h>

struct dir_worker { znode_t *parent; int error; };
static void *lock_worker(void *arg)
{
    struct dir_worker *w = arg;
    for (unsigned i = 0; i < 100 && !w->error; i++) {
        znode_t *child = NULL; zfs_dirlock_t *lock = NULL;
        char name[] = "hello.txt";
        w->error = zfs_dirent_lock(&lock, w->parent, name, &child, ZEXISTS, NULL, NULL);
        if (!w->error) {
            mutex_enter(&w->parent->z_lock);
            if (w->parent->z_dirlocks != lock || lock->dl_next || lock->dl_sharecnt)
                w->error = EIO;
            mutex_exit(&w->parent->z_lock);
        }
        if (child) oz_znode_rele(child);
        if (lock) zfs_dirent_unlock(lock);
    }
    return (NULL);
}

int oz_dirlock_test(zfsvfs_t *fs)
{
    znode_t *parent = NULL, *a = NULL, *b = NULL;
    zfs_dirlock_t *first = NULL, *second = NULL;
    int error = zfs_zget(fs, fs->z_root, &parent);
    if (error) return (error);
    char name1[] = "hello.txt", name2[] = "hello.txt";
    error = zfs_dirent_lock(&first, parent, name1, &a, ZEXISTS | ZSHARED, NULL, NULL);
    if (!error) error = zfs_dirent_lock(&second, parent, name2, &b, ZEXISTS | ZSHARED, NULL, NULL);
    if (!error && (first != second || a != b || first->dl_sharecnt != 2 ||
        first->dl_namesize != sizeof (name1) || first->dl_name == name1 ||
        first->dl_name == name2)) error = EIO;
    if (first) zfs_dirent_unlock(first);
    memset(name1, 'x', sizeof (name1));
    if (!error && strcmp(second->dl_name, "hello.txt")) error = EIO;
    if (second) zfs_dirent_unlock(second);
    if (a) oz_znode_rele(a);
    if (b) oz_znode_rele(b);
    first = NULL; a = NULL;
    if (!error && zfs_dirent_lock(&first, parent, "hello.txt", &a, ZNEW, NULL, NULL) != EEXIST)
        error = EIO;
    if (first) zfs_dirent_unlock(first);
    if (a) oz_znode_rele(a);
    first = NULL; a = NULL;
    if (!error && zfs_dirent_lock(&first, parent, "absent", &a, ZEXISTS, NULL, NULL) != ENOENT)
        error = EIO;
    if (first) zfs_dirent_unlock(first);
    if (a) oz_znode_rele(a);
    first = NULL; a = NULL;
    if (!error) error = zfs_dirent_lock(&first, parent, "absent", &a, ZNEW, NULL, NULL);
    if (!error && (a || !first)) error = EIO;
    if (first) zfs_dirent_unlock(first);
    if (a) oz_znode_rele(a);
    struct dir_worker workers[4] = {{.parent=parent},{.parent=parent},
        {.parent=parent},{.parent=parent}};
    pthread_t threads[4]; unsigned started = 0;
    if (!error) for (; started < 4; started++) {
        error = pthread_create(&threads[started], NULL, lock_worker, &workers[started]);
        if (error) break;
    }
    for (unsigned i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
        if (!error) error = workers[i].error;
    }
    if (!error && parent->z_dirlocks) error = EIO;
    oz_znode_rele(parent);
    char text[100];snprintf(text,sizeof (text),"Native directory shared/exclusive lifetime=%d\n",error);
    for (char *p=text; *p; p++) RawPutChar(*p);
    return (error);
}
