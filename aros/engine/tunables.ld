/* AROS loads relocatable ELF. Preserve the upstream linker-set registry
 * and give it section-relative bounds before the final AROS link. */
SECTIONS
{
    zfs_tunables : ALIGN(8)
    {
        __start_zfs_tunables = .;
        KEEP(*(zfs_tunables))
        __stop_zfs_tunables = .;
    }
}
INSERT AFTER .data;
