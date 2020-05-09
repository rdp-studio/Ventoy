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

if [ -f $VTOY_PATH/autoinstall ]; then    
    if [ -f /linuxrc.config ]; then
        echo "AutoYaST: file:///ventoy/autoinstall" >> /info-ventoy
        $SED "1 iinfo: file:/info-ventoy" -i /linuxrc.config
    fi
fi


#echo "Exec: /bin/sh $VTOY_PATH/hook/suse/cdrom-hook.sh" >> /info-ventoy
#echo "install: hd:/?device=/dev/mapper/ventoy" >> /info-ventoy
#$SED "1 iinfo: file:/info-ventoy" -i /linuxrc.config

ventoy_systemd_udevd_work_around
ventoy_add_udev_rule "$VTOY_PATH/hook/suse/udev_disk_hook.sh %k"
