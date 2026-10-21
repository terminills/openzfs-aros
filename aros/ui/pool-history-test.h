static int history_page(uint64_t cursor,ULONG flags,struct oz_message *out)
{
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return ENOMEM;
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=33;m->flags=flags;m->cursor=cursor;strcpy(m->target,"qemu_mirror");
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);free(m);
    if(!error)error=poll_result(&ticket,out);
    if(ticket.number){int rc=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=rc;}
    return error;
}
static int history_tests(int write)
{
    struct oz_message *out=calloc(1,sizeof(*out));int error=0;
    uint64_t cursor=0,last=0,second=0;unsigned pages=0,records=0;int comment=0,user=0,abbreviated=0;
#define HIST_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    HIST_CHECK(out);
    if(write) {
        char arg[448]="org.aros:historylong=";size_t prefix=strlen(arg);
        memset(arg+prefix,'X',400);arg[prefix+400]=0;
        HIST_CHECK(!worker_argument_test(32,"qemu_mirror",arg,OZ_OK,0));
    }
    HIST_CHECK(legacy(33,"qemu_mirror",out)==EIO&&out->status==OZ_UNSUPPORTED);
    do {
        HIST_CHECK(!history_page(cursor,0,out)&&out->status==OZ_OK);
        HIST_CHECK(out->row_count&&out->row_count<=OZ_MAX_ROWS);
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *row=&out->rows[i];unsigned long long offset=0;
            HIST_CHECK(!strcmp(row->kind,"history")&&sscanf(row->state,"offset=%llu",&offset)==1);
            HIST_CHECK(!records||offset>last);last=offset;
            if(records==1)second=offset;
            HIST_CHECK(row->used>0);
            if(strstr(row->value,"comment=AROS native pool=a;b"))comment=1;
            if(strstr(row->value,"org.aros:pool=literal=a;b"))user=1;
            if(strstr(row->value,"org.aros:historylong=")&&strstr(row->value,"..."))abbreviated=1;
            records++;
        }
        HIST_CHECK(!out->next_cursor||out->next_cursor>last);
        cursor=out->next_cursor;HIST_CHECK(++pages<256);
    }while(cursor);
    HIST_CHECK(pages>1&&records>OZ_MAX_ROWS&&comment&&user&&abbreviated);
    /* Byte-wise raw pagination and framing independently agree with the
     * decoded summary's second-record offset. No shell output parsing. */
    uint64_t position=0,length=0,frame=0,left=0;unsigned first=0,header=0,raw_records=0,xrun=0,maxrun=0;
    for(unsigned page=0;;page++) {
        HIST_CHECK(page<256);
        HIST_CHECK(!history_page(position,OZ_RAW,out)&&out->status==OZ_OK&&out->row_count);
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *row=&out->rows[i];
            HIST_CHECK(!strcmp(row->kind,"history.raw")&&strtoull(row->name,NULL,10)==position);
            HIST_CHECK(row->total&&row->total<=96&&strlen(row->value)==row->total*2);
            for(unsigned n=0;n<row->total;n++) {
                const char *hex="0123456789abcdef";
                const char *a=strchr(hex,row->value[2*n]),*b=strchr(hex,row->value[2*n+1]);
                HIST_CHECK(a&&b);
                unsigned byte=(a-hex)*16+(b-hex);
                if(byte=='X'){xrun++;if(xrun>maxrun)maxrun=xrun;}else xrun=0;
                if(first<8)length|=(uint64_t)byte<<(8*first++);
                if(left)left--;
                else {
                    frame|=(uint64_t)byte<<(8*header++);
                    if(header==8) {
                        HIST_CHECK(frame>0&&frame<=1024*1024);
                        left=frame;frame=0;header=0;raw_records++;
                    }
                }
            }
            position+=row->total;
        }
        if(!out->next_cursor)break;
        HIST_CHECK(out->next_cursor==position);
    }
    HIST_CHECK(first==8&&length>0&&length+8==second&&!left&&!header&&raw_records==records&&maxrun==400);
    HIST_CHECK(!history_page(UINT64_MAX,0,out)&&out->status==OZ_ENGINE_ERROR&&
        strstr(out->detail,"errno=22")&&!out->row_count&&!out->next_cursor);
    HIST_CHECK(!history_page(1,0,out)&&out->status==OZ_ENGINE_ERROR&&!out->row_count);
    HIST_CHECK(history_page(0,OZ_FORCE,out)==ENOTSUP);
    HIST_CHECK(history_page(0,OZ_RECURSIVE,out)==ENOTSUP);
done:
    {char line[128];snprintf(line,sizeof(line),"Native pool history write=%d pages=%u records=%u result=%d\n",write,pages,records,error);
    for(char *p=line;*p;p++)RawPutChar(*p);}
    free(out);return error;
#undef HIST_CHECK
}
