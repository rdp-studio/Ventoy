#!/ventoy/busybox/sh
#************************************************************************************
# Copyright (c) 2020, longpanda <admin@ventoy.net>
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 3 of the
# License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# 
#************************************************************************************

. /ventoy/hook/ventoy-hook-lib.sh

if is_ventoy_hook_finished; then
    exit 0
fi

VTPATH_OLD=$PATH; PATH=$BUSYBOX_PATH:$VTOY_PATH/tool:$PATH

if is_inotify_ventoy_part $3; then

    vtlog "##### INOTIFYD: $2/$3 is created (YES) ..."

    vtGenRulFile='/etc/udev/rules.d/99-live-squash.rules'
    if [ -e $vtGenRulFile ] && $GREP -q dmsquash $vtGenRulFile; then
        vtScript=$($GREP -m1 'RUN.=' $vtGenRulFile | $AWK -F'RUN.=' '{print $2}' | $SED 's/"\(.*\)".*/\1/')
        vtlog "vtScript=$vtScript"
        $vtScript
    else
        vtlog "$vtGenRulFile not exist..."
    fi

    vtlog "find ventoy partition ..."
    $BUSYBOX_PATH/sh $VTOY_PATH/hook/default/udev_disk_hook.sh $3 noreplace
    
    blkdev_num=$($VTOY_PATH/tool/dmsetup ls | grep ventoy | sed 's/.*(\([0-9][0-9]*\),.*\([0-9][0-9]*\).*/\1:\2/')  
    vtDM=$(ventoy_find_dm_id ${blkdev_num})

    if [ "$vtDM" = "dm-0" ]; then
        vtlog "This is dm-0, OK ..."
    else
        vtlog "####### This is $vtDM ####### this is abnormal ..."
        ventoy_swap_device /dev/dm-0 /dev/$vtDM
    fi
    
    if [ -e /sbin/anaconda-diskroot ]; then
        vtlog "set anaconda-diskroot ..."
        /sbin/anaconda-diskroot /dev/dm-0    
    fi
    
    set_ventoy_hook_finish
else
    vtlog "##### INOTIFYD: $2/$3 is created (NO) ..."
fi

PATH=$VTPATH_OLD
