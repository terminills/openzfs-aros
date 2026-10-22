/* Build evidence only: these values must match the target runtime's SDK.
 * A changed Task/MsgPort layout also requires rebuilding static dependencies
 * and startup archives; relinking old objects is not sufficient.
 */
#include <stddef.h>
#include <exec/tasks.h>
#include <exec/ports.h>
#include <dos/dosextens.h>

const unsigned long oz_abi_task_size = sizeof(struct Task);
const unsigned long oz_abi_process_size = sizeof(struct Process);
const unsigned long oz_abi_process_msgport = offsetof(struct Process, pr_MsgPort);
const unsigned long oz_abi_process_cli = offsetof(struct Process, pr_CLI);
const unsigned long oz_abi_msgport_size = sizeof(struct MsgPort);
const unsigned long oz_abi_msgport_list = offsetof(struct MsgPort, mp_MsgList);
