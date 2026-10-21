/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <string.h>
#include <aros/debug.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){bug("RESULT native CLI upstream process FAIL line=%d\n",__LINE__);return 20;}}while(0)
int main(void)
{
    for(unsigned i=0;i<8;i++) {
        char *args[]={"output-child","lines",NULL};
        char *env[]={"PATH=/C",NULL};char **lines=NULL;int count=0;
        int status=libzfs_run_process_get_stdout("OpenZFSOutputChild",args,env,&lines,&count);
        bug("CLI upstream search status=%d count=%d\n",status,count);
        CHECK(status==23);
        CHECK(count==3&&!strcmp(lines[0],"alpha")&&!strcmp(lines[1],"")&&!strcmp(lines[2],"omega"));
        libzfs_free_str_array(lines,count);
        CHECK(libzfs_run_process_get_stdout_nopath("C:OpenZFSOutputChild",args,env,&lines,&count)==23);
        CHECK(count==3&&!strcmp(lines[2],"omega"));libzfs_free_str_array(lines,count);
        CHECK(libzfs_run_process_get_stdout_nopath("OpenZFSOutputChild",args,env,&lines,&count)==255);
        CHECK(!count);libzfs_free_str_array(lines,count);
        CHECK(libzfs_run_process("C:OpenZFSOutputChild",args,NO_DEFAULT_PATH)==23);
        CHECK(libzfs_run_process("C:OpenZFSAbsentChild",args,NO_DEFAULT_PATH)==255);
        char *literal_args[]={"argv-child","","two words","quote\"star*",
            "line\nbreak","tab\there","$HOME;`literal`","SYS:pool/path",NULL};
        CHECK(libzfs_run_process_get_stdout_nopath("C:OpenZFSArgvChild",literal_args,
            env,&lines,&count)==17);
        CHECK(count==1&&!strcmp(lines[0],"argv-child-output"));
        libzfs_free_str_array(lines,count);
        char *child_args[]={"environment-child","explicit",NULL};
        char *child_env[]={"PATH=/C","OZ_CLI_ENV_VALUE=child-value","OZ_CLI_EMPTY=",
            "OZ_CLI_LITERAL=$HOME;`literal`",NULL};
        CHECK(!setenv("OZ_CLI_PARENT_ONLY","not-for-child",1));
        CHECK(libzfs_run_process_get_stdout("OpenZFSEnvironmentChild",child_args,
            child_env,&lines,&count)==29);
        CHECK(!count);libzfs_free_str_array(lines,count);
        CHECK(!strcmp(getenv("OZ_CLI_PARENT_ONLY"),"not-for-child"));
        CHECK(!unsetenv("OZ_CLI_PARENT_ONLY"));
    }
    bug("RESULT native CLI upstream process PASS runs=8 search=1 exact=1 status=1\n");
    return 0;
}
