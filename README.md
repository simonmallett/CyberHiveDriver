
CyberHive Technical Challange


**This work is not associated or meant to be representative/attributed to the company CyberHive in any way.
Was just a tech exercise*

Kernel Mode Driver that outputs to debug any file names that are opened that are equal to CyberHive.


Considerations:
Kernel mode drivers should be quick to do what they need to do and push requests down driver stack promptly.
Should have safe memory allocation.
Use string safe library (prevent buffer overflows where possible)
Tested on VM (Windows doesnt like broken drivers)



Method:
Started by using the default WDK empty kernel mode driver, (includes the tracing code and .inf) then appropriated boilerplate functions that are used to attach/detach the driver to mounted volumes. 
The driver stubs off the FastIo callbacks with passthroughs (this template code is taken from apriorit.com's basic driver template tutorial using FastIo.c). The IRP table is filled with passthrough callbacks except the IRP_MJ_CREATE which is used to grab events when opening files. we then check the filename and output to debug.



Todo:
Consider implementing a mini filter instead of full driver using FLT library (no need to deal with fastIo callbacks and already has many implemented checks)
Do in string comparison of name, instead of full equal comparison (using stringsafe functions)
Consider communications to user mode process to send notifications of used files for user display (akin to minispy in WDK tutorials)


Conclusion:
Would likely be faster to use a minifilter as you dont have to implement code for all the fastio callbacks and already has comm's to user mode built in. Minifilters also have control over altitude on the minifilter stack so you can control order of driver use. Also can be unloaded safely.
Thought it would be more interesting to try to implement the full driver.