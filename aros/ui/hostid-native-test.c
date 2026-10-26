/* SPDX-License-Identifier: CDDL-1.0 */
/* Disposable, configured-security QEMU fixture only. Never a user session. */
#include "hostid-settings.h"
#include "api.h"
#include "admission-api.h"
#include "../engine/hostid-record.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <aros/debug.h>
#include <errno.h>

static int record_is(const char *path, uint32_t expected)
{
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!file) return 0;
    uint32_t value = 0;
    int error = oz_hostid_record_read(file, &value);
    int closed = Close(file);
    return !error && closed && value == expected;
}

int main(void)
{
    struct Library *secBase = NULL;
    int failed = 0, logged = 0;
    unsigned published = 99;
    ULONG previous_owner = 0;
    BPTR transaction = 0;
    struct MsgPort *fixture_port = NULL;
    int port_added = 0;
    struct TagItem root[] = {{secT_UserID,(IPTR)"oztestroot"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem reader[] = {{secT_UserID,(IPTR)"ozreader"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem logout[] = {{secT_Quiet,TRUE},{TAG_DONE,0}};
#define CHECK(x) do { if (!(x)) { failed = __LINE__; goto done; } } while (0)
    bug("RESULT host identity BEGIN RAM-only=1\n");
    secBase = OpenLibrary((CONST_STRPTR)SECURITYNAME, SECURITYVERSION);
    CHECK(secBase && secIsConfigured());
    previous_owner = secGetTaskOwner(FindTask(NULL));
    CHECK(secLoginA(root) == secOWNER_SYSTEM); logged = 1;
    CHECK(SetOwner((STRPTR)"RAM:ENV", secOWNER_SYSTEM));
    CHECK(SetOwner((STRPTR)"RAM:ENVARC", secOWNER_SYSTEM));
    CHECK(secSetProtection((CONST_STRPTR)"RAM:ENV", 0));
    CHECK(secSetProtection((CONST_STRPTR)"RAM:ENVARC", 0));
    CHECK(oz_hostid_settings_apply(0, &published) == EINVAL && !published);
    CHECK(!oz_hostid_settings_apply(0x11223344, &published) && published == 3);
    CHECK(record_is("ENV:OpenZFSHost.prefs", 0x11223344));
    CHECK(record_is("ENVARC:OpenZFSHost.prefs", 0x11223344));
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 00000000 SAVE >NIL:", TAG_DONE) == 20);
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 11223344 >NIL:", TAG_DONE) == 20);
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity z1223344 SAVE >NIL:", TAG_DONE) == 20);
    CHECK(record_is("ENV:OpenZFSHost.prefs", 0x11223344));
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 55667788 SAVE >NIL:", TAG_DONE) == 0);
    CHECK(record_is("ENV:OpenZFSHost.prefs", 0x55667788));
    CHECK(record_is("ENVARC:OpenZFSHost.prefs", 0x55667788));
    bug("RESULT host identity valid-cli PASS invalid-cli=3\n");
    transaction = CreateDir((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn");
    CHECK(transaction); UnLock(transaction); transaction = 0;
    published = 99;
    CHECK(oz_hostid_settings_apply(0xaabbccdd, &published) == EBUSY && published == OZ_HOSTID_SAVED);
    CHECK(record_is("ENVARC:OpenZFSHost.prefs", 0xaabbccdd));
    CHECK(record_is("ENV:OpenZFSHost.prefs", 0x55667788));
    transaction = Lock((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn", SHARED_LOCK);
    CHECK(transaction); UnLock(transaction); transaction = 0;
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn"));
    CHECK(!oz_hostid_settings_apply(0xaabbccdd, &published) && published == 3);
    bug("RESULT host identity partial-save PASS saved=1 current=0 collision-preserved=1 recovered=1\n");
    /* Named-port fixtures exercise the real publication guard, not a running
     * engine. This isolated guest has no management service or clients. */
    const char *names[] = {OZ_PORT ".Starting", OZ_PORT, OZ_ADMISSION_PORT};
    fixture_port = CreateMsgPort();
    CHECK(fixture_port);
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        CHECK(!FindPort((CONST_STRPTR)names[i]));
        fixture_port->mp_Node.ln_Name = (char *)names[i];
        AddPort(fixture_port); port_added = 1;
        published = 99;
        CHECK(oz_hostid_settings_apply(0x12345678, &published) == EBUSY && !published);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 12345678 SAVE >NIL:", TAG_DONE) == 20);
        CHECK(record_is("ENV:OpenZFSHost.prefs", 0xaabbccdd));
        CHECK(record_is("ENVARC:OpenZFSHost.prefs", 0xaabbccdd));
        transaction = Lock((CONST_STRPTR)"ENV:OpenZFSHost.update", SHARED_LOCK);
        CHECK(!transaction && IoErr() == ERROR_OBJECT_NOT_FOUND);
        RemPort(fixture_port); port_added = 0;
    }
    DeleteMsgPort(fixture_port); fixture_port = NULL;
    transaction = CreateDir((CONST_STRPTR)"ENV:OpenZFSHost.update");
    CHECK(transaction); UnLock(transaction); transaction = 0;
    published = 99;
    CHECK(oz_hostid_settings_apply(0x12345678, &published) == EBUSY && !published);
    transaction = Lock((CONST_STRPTR)"ENV:OpenZFSHost.update", SHARED_LOCK);
    CHECK(transaction); UnLock(transaction); transaction = 0;
    CHECK(record_is("ENV:OpenZFSHost.prefs", 0xaabbccdd));
    CHECK(record_is("ENVARC:OpenZFSHost.prefs", 0xaabbccdd));
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSHost.update"));
    CHECK(!oz_hostid_settings_apply(0xaabbccdd, &published) && published == 3);
    bug("RESULT host identity guards PASS port-fixtures=3 competing-writer=1 retry=1\n");
    CHECK(secLogoutA(logout) == previous_owner); logged = 0;
    CHECK(secLoginA(reader) == ((77UL << 16) | 88)); logged = 1;
    published = 99;
    CHECK(oz_hostid_settings_apply(0x12345678, &published) == EPERM && !published);
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 12345678 SAVE >NIL:", TAG_DONE) == 20);
    CHECK(secLogoutA(logout) == previous_owner); logged = 0;
    CHECK(secLoginA(root) == secOWNER_SYSTEM); logged = 1;
    CHECK(record_is("ENV:OpenZFSHost.prefs", 0xaabbccdd));
    CHECK(record_is("ENVARC:OpenZFSHost.prefs", 0xaabbccdd));
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSHost.prefs"));
    CHECK(DeleteFile((CONST_STRPTR)"ENVARC:OpenZFSHost.prefs"));
    transaction = Lock((CONST_STRPTR)"ENV:OpenZFSHost.update", SHARED_LOCK);
    CHECK(!transaction && IoErr() == ERROR_OBJECT_NOT_FOUND);
done:
    if (port_added) RemPort(fixture_port);
    if (fixture_port) DeleteMsgPort(fixture_port);
    if (transaction) UnLock(transaction);
    if (logged) secLogoutA(logout);
    if (secBase) CloseLibrary(secBase);
    bug("RESULT host identity %s line=%d authority=checked persistence=RAM-only\n", failed ? "FAIL" : "PASS", failed);
    return failed ? 20 : 0;
}
