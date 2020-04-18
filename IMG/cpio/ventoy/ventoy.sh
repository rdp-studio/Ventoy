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

####################################################################
#                                                                  #
# Step 1 : Parse kernel parameter                                  #
#                                                                  #
####################################################################
if ! [ -e /proc ]; then
    $BUSYBOX_PATH/mkdir /proc
    rmproc='Y'
fi
$BUSYBOX_PATH/mount -t proc proc /proc

# vtinit=xxx to replace rdinit=xxx
vtcmdline=$($CAT /proc/cmdline)
for i in $vtcmdline; do
    if echo $i | $GREP -q vtinit; then
        user_rdinit=${i#vtinit=}
        echo "user set user_rdinit=${user_rdinit}" >>$VTLOG
    fi
done


####################################################################
#                                                                  #
# Step 2 : Do OS specific hook                                     #
#                                                                  #
####################################################################
ventoy_get_os_type() {
    echo "kernel version" >> $VTLOG
    $CAT /proc/version >> $VTLOG

    # rhel5/CentOS5 and all other distributions based on them
    if $GREP -q 'el5' /proc/version; then
        echo 'rhel5'; return

    # rhel6/CentOS6 and all other distributions based on them
    elif $GREP -q 'el6' /proc/version; then
        echo 'rhel6'; return

    # rhel7/CentOS7/rhel8/CentOS8 and all other distributions based on them
    elif $GREP -q 'el[78]' /proc/version; then
        echo 'rhel7'; return   

    # Maybe rhel9 rhel1x use the same way? Who knows!
    elif $EGREP -q 'el9|el1[0-9]' /proc/version; then
        echo 'rhel7'; return   
        
    # Fedora : do the same process with rhel7
    elif $GREP -q '\.fc[0-9][0-9]\.' /proc/version; then
        echo 'rhel7'; return
        
    # Debian :
    elif $GREP -q '[Dd]ebian' /proc/version; then
        echo 'debian'; return
        
    # Ubuntu : do the same process with debian
    elif $GREP -q '[Uu]buntu' /proc/version; then
        echo 'debian'; return
        
    # Deepin : do the same process with debian
    elif $GREP -q '[Dd]eepin' /proc/version; then
        echo 'debian'; return
        
    # SUSE
    elif $GREP -q 'SUSE' /proc/version; then
        echo 'suse'; return
        
    # ArchLinux
    elif $EGREP -q 'archlinux|ARCH' /proc/version; then
        echo 'arch'; return
    
    # gentoo
    elif $EGREP -q '[Gg]entoo' /proc/version; then
        echo 'gentoo'; return
        
    # TinyCore
    elif $EGREP -q 'tinycore' /proc/version; then
        echo 'tinycore'; return
    
    # manjaro
    elif $EGREP -q 'manjaro|MANJARO' /proc/version; then
        echo 'manjaro'; return
        
    # mageia
    elif $EGREP -q 'mageia' /proc/version; then
        echo 'mageia'; return
    
    # pclinux OS
    elif $GREP -i -q 'PCLinuxOS' /proc/version; then
        echo 'pclos'; return
    
    # KaOS
    elif $GREP -i -q 'kaos' /proc/version; then
        echo 'kaos'; return
    
    # Alpine
    elif $GREP -q 'Alpine' /proc/version; then
        echo 'alpine'; return

    # NixOS
    elif $GREP -i -q 'NixOS' /proc/version; then
        echo 'nixos'; return
    
    fi

    if [ -e /lib/debian-installer ]; then
        echo 'debian'; return
    fi

    if [ -e /etc/os-release ]; then
        if $GREP -q 'XenServer' /etc/os-release; then
            echo 'xen'; return
        elif $GREP -q 'SUSE ' /etc/os-release; then
            echo 'suse'; return
        fi
    fi
    
    if $BUSYBOX_PATH/dmesg | $GREP -q -m1 "Xen:"; then
        echo 'xen'; return
    fi
    
    
    if [ -e /etc/HOSTNAME ] && $GREP -i -q 'slackware' /etc/HOSTNAME; then
        echo 'slackware'; return
    fi
    
    if [ -e /init ]; then
        if $GREP -i -q zeroshell /init; then
            echo 'zeroshell'; return
        fi
    fi
    
    if $EGREP -q 'ALT ' /proc/version; then
        echo 'alt'; return
    fi
    
    if $EGREP -q 'porteus' /proc/version; then
        echo 'debian'; return
    fi
    
    echo "default"
}

VTOS=$(ventoy_get_os_type)
echo "OS=###${VTOS}###" >>$VTLOG
if [ -e "$VTOY_PATH/hook/$VTOS/ventoy-hook.sh" ]; then
    $BUSYBOX_PATH/sh "$VTOY_PATH/hook/$VTOS/ventoy-hook.sh"
fi


####################################################################
#                                                                  #
# Step 3 : Check for debug break                                   #
#                                                                  #
####################################################################
if [ "$VTOY_BREAK_LEVEL" = "03" ] || [ "$VTOY_BREAK_LEVEL" = "13" ]; then
    $SLEEP 5
    echo -e "\n\n\033[32m ################################################# \033[0m"
    echo -e "\033[32m ################ VENTOY DEBUG ################### \033[0m"
    echo -e "\033[32m ################################################# \033[0m \n"
    if [ "$VTOY_BREAK_LEVEL" = "13" ]; then 
        $CAT $VTOY_PATH/log
    fi
    exec $BUSYBOX_PATH/sh
fi



####################################################################
#                                                                  #
# Step 4 : Hand over to real init                                  #
#                                                                  #
####################################################################
$BUSYBOX_PATH/umount /proc
if [ "$rmproc" = "Y" ]; then
    $BUSYBOX_PATH/rm -rf /proc
fi

cd /
unset VTOY_PATH VTLOG FIND GREP EGREP CAT AWK SED SLEEP HEAD

for vtinit in $user_rdinit /init /sbin/init /linuxrc; do
    if [ -d /ventoy_rdroot ]; then
        if [ -e "/ventoy_rdroot$vtinit" ]; then
            # switch_root will check /init file, this is a cheat code
            echo 'switch_root' > /init
            exec $BUSYBOX_PATH/switch_root /ventoy_rdroot "$vtinit"
        fi
    else
        if [ -e "$vtinit" ];then
            exec "$vtinit"
        fi
    fi
done

# Should never reach here
echo -e "\n\n\033[31m ############ INIT NOT FOUND ############### \033[0m \n"
exec $BUSYBOX_PATH/sh
