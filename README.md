# Ventoy
Ventoy is an open source tool to create bootable USB drive for ISO files.   
With ventoy, you don't need to format the disk again and again, you just need to copy the iso file to the USB drive and boot it.   
You can copy many iso files at a time and ventoy will give you a boot menu to select them.  
Both Legacy BIOS and UEFI are supported in the same way. 200+ ISO files are tested.  
A "Ventoy Compatible" concept is introduced by ventoy, which can help to support any ISO file.  

See https://www.ventoy.net for detail.

# Features
* 100% open source
* Simple to use
* Fast (limited only by the speed of copying iso file)
* Directly boot from iso file, no extraction needed
* Legacy + UEFI supported in the same way
* UEFI Secure Boot supported (since 1.0.07+) Notes
* ISO files larger than 4GB supported
* Native boot menu style for Legacy & UEFI
* Most type of OS supported, 200+ iso files tested
* Not only boot but also complete installation process
* "Ventoy Compatible" concept
* Plugin Framework
* Readonly to USB drive during boot
* USB normal use unafftected
* Data nondestructive during version upgrade
* No need to update Ventoy when a new distro is released
