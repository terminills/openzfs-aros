#!/usr/bin/env bash
# Source before allocating test artifacts. Reserve is not a test size estimate.
oz_artifact_root()
{
    local default=$1 external=${2:-} destination=$1 system_device destination_device
    if [[ -n $external ]]; then
        destination=$(realpath -e "$external") || return 1
        [[ -d $destination ]] || { echo 'Artifact root must be a directory' >&2; return 1; }
        system_device=$(stat -c %d /) || return 1
        destination_device=$(stat -c %d "$destination") || return 1
        if [[ ! $system_device =~ ^[0-9]+$ || ! $destination_device =~ ^[0-9]+$ ]]; then
            echo 'Cannot verify artifact filesystem identity' >&2
            return 1
        fi
        # A checkout may itself live on the external filesystem. Compare to
        # the system filesystem, not the checkout: an unmounted destination
        # must still fail closed instead of allocating on the system disk.
        if [[ $destination_device == "$system_device" ]]; then
            echo 'External artifact root must reside outside the system filesystem' >&2
            return 1
        fi
        [[ -w $destination ]] || { echo 'External artifact root must be writable' >&2; return 1; }
        oz_storage_preflight / || return 1
    fi
    oz_storage_preflight "$default" || return 1
    oz_storage_preflight "$destination" || return 1
    printf '%s\n' "$destination"
}

oz_storage_preflight()
{
    local path=$1 available
    local reserve_kib=104857600 # 100 GiB host headroom
    available=$(LC_ALL=C df -Pk -- "$path" | awk 'NR==2 {print $4}') || return 1
    if [[ ! $available =~ ^[0-9]+$ ]]; then
        echo "Cannot verify available storage for $path; refusing test allocation" >&2
        return 1
    fi
    if (( available < reserve_kib )); then
        echo "Insufficient storage: $available KiB free at $path; retain at least $reserve_kib KiB. Archive inactive artifacts to the designated external drive first." >&2
        return 1
    fi
}
