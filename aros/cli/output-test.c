/* SPDX-License-Identifier: CDDL-1.0 */
#include "process.h"
#include <proto/dos.h>
#include <aros/debug.h>
#include <errno.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI output FAIL line=%d\n", __LINE__); return 20; } } while (0)
int main(void)
{
    char *modes[] = {"empty", "lines", "large", "nul"};
    for (unsigned int i = 0; i < 4; ++i) {
        BPTR output = Open((CONST_STRPTR)"RAM:cli-output-test", MODE_NEWFILE);
        BPTR errors = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
        struct oz_cli_result result;
        char *args[] = {"output-child", modes[i], NULL};
        CHECK(output && errors);
        CHECK(oz_cli_spawn_exact("C:OpenZFSOutputChild", args, output, errors, &result) == 0);
        CHECK(result.status == 23);
        CHECK(Flush(output));
        CHECK(Seek(output, 0, OFFSET_BEGINNING) != -1);
        char **lines = NULL; int count = 0;
        int error = oz_cli_read_lines(output, 200000, &lines, &count);
        if (i == 3) {
            CHECK(error == EILSEQ && lines == NULL && count == 0);
        } else {
            CHECK(error == 0);
            if (i == 0) CHECK(count == 0 && lines == NULL);
            if (i == 1) CHECK(count == 3 && !strcmp(lines[0], "alpha") &&
                !strcmp(lines[1], "") && !strcmp(lines[2], "omega"));
            if (i == 2) {
                CHECK(count == 2 && strlen(lines[0]) == 131072 && !strcmp(lines[1], "tail"));
                for (size_t j = 0; j < 131072; ++j) CHECK(lines[0][j] == 'x');
            }
        }
        oz_cli_free_lines(lines, count);
        if (i == 2) {
            CHECK(Seek(output, 0, OFFSET_BEGINNING) != -1);
            CHECK(oz_cli_read_lines(output, 1024, &lines, &count) == EOVERFLOW);
            CHECK(lines == NULL && count == 0);
        }
        CHECK(Close(output)); CHECK(Close(errors));
    }
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-output-test"));
    BPTR previous_temp=Lock((CONST_STRPTR)"T:",SHARED_LOCK);
    BPTR private_temp=CreateDir((CONST_STRPTR)"RAM:cli-capture-temp");
    CHECK(previous_temp&&private_temp&&AssignLock((CONST_STRPTR)"T",private_temp));
    for(unsigned cycle=0;cycle<16;cycle++) {
        BPTR errors=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);CHECK(errors);
        struct oz_cli_result result;char **lines=NULL;int count=0;
        char *args[]={"output-child","lines",NULL};char *empty_env[]={NULL};
        CHECK(!oz_cli_capture_env_exact("C:OpenZFSOutputChild",args,empty_env,errors,
            200000,&lines,&count,&result));
        CHECK(result.status==23&&count==3&&!strcmp(lines[0],"alpha")&&
            !strcmp(lines[1],"")&&!strcmp(lines[2],"omega"));
        oz_cli_free_lines(lines,count);
        args[1]="large";
        CHECK(!oz_cli_capture_env_exact("C:OpenZFSOutputChild",args,NULL,errors,
            200000,&lines,&count,&result));
        CHECK(result.status==23&&count==2&&strlen(lines[0])==131072&&!strcmp(lines[1],"tail"));
        oz_cli_free_lines(lines,count);
        CHECK(oz_cli_capture_env_exact("C:OpenZFSOutputChild",args,NULL,errors,
            1024,&lines,&count,&result)==EOVERFLOW&&result.status==23&&!lines&&!count);
        args[1]="nul";
        CHECK(oz_cli_capture_env_exact("C:OpenZFSOutputChild",args,NULL,errors,
            200000,&lines,&count,&result)==EILSEQ&&result.status==23&&!lines&&!count);
        CHECK(oz_cli_capture_env_exact("C:OpenZFSAbsentCaptureChild",args,NULL,errors,
            200000,&lines,&count,&result)==ENOEXEC&&!lines&&!count);
        CHECK(Close(errors));
    }
    CHECK(AssignLock((CONST_STRPTR)"T",previous_temp));
    /* Directory removal proves all successful/failed captures cleaned up. */
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-capture-temp"));
    bug("RESULT native CLI capture PASS runs=16 cleanup=1\n");
    bug("RESULT native CLI output PASS\n");
    return 0;
}
