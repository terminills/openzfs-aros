#ifndef OZ_API_H
#define OZ_API_H
#include <exec/ports.h>
#include <time.h>
#define OZ_PORT "OpenZFS.Management.v3"
#define OZ_MAGIC 0x4f5a4d31UL
#define OZ_VERSION 3
#define OZ_TEST_PORT "OpenZFS.Management.TestDrop.v3"
#define OZ_NATIVE_TEST_PORT "OpenZFS.Management.NativeTest.v3"
#define OZ_MAX_GROUPS 8
#define OZ_MAX_DEVICES 32
enum { OZ_DATA, OZ_LOG, OZ_CACHE, OZ_SPARE, OZ_SPECIAL, OZ_DEDUP };
enum { OZ_STRIPE, OZ_MIRROR, OZ_RAIDZ1, OZ_RAIDZ2, OZ_RAIDZ3, OZ_DRAID };
/* dRAID: parity 1..3, data=0 selects upstream default min(8,available),
 * spares is a count of distributed spares, not separate spare leaf paths.
 * All three fields must be zero for other layouts. */
struct oz_vdev_group { ULONG role, layout, draid_parity, draid_data, draid_spares; };
struct oz_vdev_leaf { ULONG group; char path[256]; };
struct oz_topology {
    ULONG group_count, device_count, ashift;
    struct oz_vdev_group groups[OZ_MAX_GROUPS];
    struct oz_vdev_leaf devices[OZ_MAX_DEVICES];
};
#define OZ_CONFIRMED 1
#define OZ_RECURSIVE 2
#define OZ_FORCE 4
#define OZ_DRYRUN 8
#define OZ_RAW 16
#define OZ_RESUMABLE 32
#define OZ_MAX_ROWS 24
/* Structured result page, independent of human-readable diagnostics. Byte counts
 * and progress are numeric; the GUI must not parse formatted command output. */
/* pool.list rows: total=raw SPA capacity, used=allocated, available=free bytes.
 * These are not dataset available bytes. State UNAVAILABLE has no numeric
 * sample and reports an error, not an empty or healthy zero-capacity pool. */
/* io-bytes/io-ops rows: used=reads, available=writes, total=elapsed ns since
 * vdev load, NOT capacity. UNAVAILABLE means no numeric sample. These stable
 * operation-specific row kinds extend the API without changing wire layout. */
struct oz_row { char kind[24],name[160],parent[160],state[48],value[256];
    UQUAD used,available,total; ULONG progress_permille; };
/* rebuild rows: used=scanned bytes, total=estimated bytes to scan,
 * available=remaining estimate. value carries issued/rebuilt/errors/times.
 * COMPLETE comes from upstream rebuild state, never estimated byte equality.
 * Sequential rebuild is not a checksum scrub; inspect scrub separately. */
/* Exact property queries may return kind=property-long. The complete value
 * is message.detail (no prose prefix), total is its byte length, and row.value
 * is display guidance only. Values exceeding detail capacity fail explicitly. */
enum { OZ_OK, OZ_NO_SERVICE, OZ_NO_ENGINE, OZ_BAD_REQUEST, OZ_BAD_VERSION,
       OZ_UNSUPPORTED, OZ_TIMEOUT, OZ_BUSY, OZ_NO_MEMORY, OZ_ENGINE_ERROR,
       OZ_CANCELLED };
struct oz_operation { ULONG id; const char *category, *key, *label; int mutation;
    const char *target_label, *argument_label, *help; };
extern const struct oz_operation oz_operations[];
extern const unsigned oz_operation_count;
/* No process-private pointers after the Exec message header. One recipient owns
 * each allocation. Reply routing is by unique port name, NOT mn_ReplyPort. */
struct oz_message {
    struct Message msg;
    ULONG magic, version, id, operation, flags, status;
    char reply_name[96], target[256], argument[512], options[1024];
    char detail[4096];
    ULONG engine_available, capability_words[4], row_count;
    UQUAD job_id, cursor, next_cursor;
    struct oz_row rows[OZ_MAX_ROWS];
    struct oz_topology topology;
};
_Static_assert(sizeof(struct oz_message)<=65535,"Exec message length overflow");
struct oz_client { struct MsgPort *port; char name[96]; const char *server_name; ULONG next_id, pending;
    time_t deadline; };
enum oz_route { OZ_ROUTE_INVALID, OZ_ROUTE_PUBLIC, OZ_ROUTE_AUTHENTICATED,
    OZ_ROUTE_PROJECT, OZ_ROUTE_UNAVAILABLE };
const struct oz_operation *oz_find(ULONG id);
/* Stable catalogue entries can outlive a platform implementation.  A non-NULL
 * result is the exact reason this AROS build must not submit the operation. */
const char *oz_operation_unavailable(ULONG id);
/* One audited transport decision for every stable operation ID.  Explicit-file
 * import and native /AROS label clearing are authenticated; legacy
 * configuration-file import and ordinary file label clearing keep their
 * established transports. */
enum oz_route oz_request_route(const struct oz_message *m);
/* Unknown actions remain conservative; only exact project queries are exempt. */
int oz_request_mutates(ULONG id, const char *argument);
const char *oz_status(ULONG status);
ULONG oz_validate(const struct oz_message *m, char *why, unsigned size);
ULONG oz_validate_topology(const struct oz_topology *t, char *why, unsigned size);
int oz_draid_geometry(const struct oz_vdev_group *, unsigned children,
    unsigned *data, unsigned *groups);
ULONG oz_validate_add_topology(const struct oz_topology *t, char *why, unsigned size);
ULONG oz_validate_import_files(const struct oz_topology *t, char *why, unsigned size);
/* Strict import options; readonly is mandatory, destroyed defaults off. */
int oz_parse_import_options(const char *,int *readonly,int *destroyed,int *rewind);
int oz_parse_import_identity(const char *,int *readonly,int *destroyed,int *rewind,UQUAD *expected_guid);
int oz_client_open(struct oz_client *c);
void oz_client_close(struct oz_client *c);
ULONG oz_submit(struct oz_client *c, const struct oz_message *input);
struct oz_message *oz_poll(struct oz_client *c);
void oz_message_free(struct oz_message *m);
/* Engine adapter boundary. Never return success until the engine completes or
 * supplies an explicit asynchronous job identity. Capabilities describe engine
 * operations, not merely the broker's recognized operation names. */
void oz_engine_dispatch(struct oz_message *m);
void oz_broker_process(struct oz_message *,int drop);
void oz_broker_process_public(struct oz_message *,int drop);
int oz_engine_start(int fixture_writes);
int oz_engine_stop(void);
ULONG oz_engine_signals(void);
void oz_engine_pump(void);
#endif
