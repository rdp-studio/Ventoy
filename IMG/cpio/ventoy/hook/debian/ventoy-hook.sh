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

. $VTOY_PATH/hook/ventoy-os-lib.sh

ventoy_get_debian_distro() {
    if [ -d /KNOPPIX ]; then
        echo 'knoppix'; return
    elif [ -e /etc/initrd-release ]; then
        if $EGREP -q "ID=.*antix|ID=.*mx" /etc/initrd-release; then
            echo 'antix'; return
        fi
    fi
    
    if [ -e /init ]; then
        if $GREP -q PUPPYSFS /init; then
            if $GREP -q VEKETSFS /init; then
                echo 'veket'; return
            else
                echo 'puppy'; return
            fi
        elif $GREP -m1 -q 'Minimal.*Linux.*Live' /init; then
            echo 'mll'; return
        fi
    fi

    if [ -e /etc/os-release ]; then
        if $GREP -q 'Tails' /etc/os-release; then
            echo 'tails'; return
        fi
    fi

    if $GREP -q 'slax/' /proc/cmdline; then
        echo 'slax'; return
    fi
    
    if $GREP -q 'PVE ' /proc/version; then
        echo 'pve'; return
    fi
    
    if [ -d /porteus ]; then
        echo 'porteus'; return
    fi
    
    if $GREP -q 'porteus' /proc/version; then
        echo 'porteus'; return
    fi
    
    if $GREP -q 'linuxconsole' /proc/version; then
        echo 'linuxconsole'; return
    fi
    
    if $GREP -q 'vyos' /proc/version; then
        echo 'vyos'; return
    fi
    
    echo 'default'
}

DISTRO=$(ventoy_get_debian_distro)

echo "##### distribution = $DISTRO ######" >> $VTLOG
. $VTOY_PATH/hook/debian/${DISTRO}-hook.sh










