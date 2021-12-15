/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_CyberHiveDriver,
    0x2fe6d056,0x7eea,0x41d5,0xb7,0x38,0x9b,0x0c,0x81,0x31,0x79,0xc5);
// {2fe6d056-7eea-41d5-b738-9b0c813179c5}
