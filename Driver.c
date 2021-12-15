/*++

Module Name:

    driver.c

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "driver.tmh"

#include "ntstrsafe.h"
#include "ntdef.h"


#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, CyberHiveDriverEvtDeviceAdd)
#pragma alloc_text (PAGE, CyberHiveDriverEvtDriverContextCleanup)
#endif

//Global reference to driver object
PDRIVER_OBJECT g_driverObject = NULL;

FAST_IO_DISPATCH g_fastIoDispatch =
{
    sizeof(FAST_IO_DISPATCH),
    FilterFastIoCheckIfPossible,
    FilterFastIoRead,
    FilterFastIoWrite,
    FilterFastIoQueryBasicInfo,
    FilterFastIoQueryStandardInfo,
    FilterFastIoLock,
    FilterFastIoUnlockSingle,
    FilterFastIoUnlockAll,
    FilterFastIoUnlockAllByKey,
    FilterFastIoDeviceControl,
    NULL,
    NULL,
    FilterFastIoDetachDevice,
    FilterFastIoQueryNetworkOpenInfo,
    NULL,
    FilterFastIoMdlRead,
    FilterFastIoMdlReadComplete,
    FilterFastIoPrepareMdlWrite,
    FilterFastIoMdlWriteComplete,
    FilterFastIoReadCompressed,
    FilterFastIoWriteCompressed,
    FilterFastIoMdlReadCompleteCompressed,
    FilterFastIoMdlWriteCompleteCompressed,
    FilterFastIoQueryOpen,
    NULL,
    NULL,
    NULL,
};

NTSTATUS CyberHiveDispatchPassThrough(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP           Irp
)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceObject->DeviceExtension, Irp);
}

VOID CyberHiveFastIoDetachDevice(
    __in PDEVICE_OBJECT     SourceDevice,
    __in PDEVICE_OBJECT     TargetDevice
)
{
    //
    //  Detach from the file system's volume device object.
    //
    IoDetachDevice(TargetDevice);
    IoDeleteDevice(SourceDevice);
} 

NTSTATUS CyberHiveDispatchCreate(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP           Irp
) 
{
    PFILE_OBJECT pFileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    UNICODE_STRING CyberHiveString;

    RtlInitUnicodeString(&CyberHiveString, L"cyberhive");

    if(RtlCompareUnicodeString(&pFileObject->FileName, &CyberHiveString, TRUE))
        DbgPrint("Attempting to open file: %wZ\n", &pFileObject->FileName);

    return CyberHiveDispatchPassThrough(DeviceObject, Irp);
}

NTSTATUS CyberHiveAttachToFileSystemDevice(
    __in PDEVICE_OBJECT DeviceObject
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    PDEVICE_OBJECT  filterDeviceObject = NULL;

    if (!CyberHiveIsAttachedToDevice(DeviceObject))
    {
        status = CyberHiveAttachToDevice(DeviceObject, &filterDeviceObject);

        if (!NT_SUCCESS(status))
        {
            return status;
        }

        //
        //  Enumerate all the mounted devices that currently exist for this file system and attach to them.
        //

        status = CyberHiveEnumerateFileSystemVolumes(DeviceObject);

        if (!NT_SUCCESS(status))
        {
            CyberHiveDetachFromDevice(filterDeviceObject);
            return status;
        }
    }

    return STATUS_SUCCESS;
}

BOOLEAN CyberHiveIsMyDeviceObject(
    __in PDEVICE_OBJECT DeviceObject
)
{
    return DeviceObject->DriverObject == g_driverObject;
}

VOID CyberHiveDetachFromFileSystemDevice(
    __in PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_OBJECT device = NULL;

    for (device = DeviceObject->AttachedDevice; NULL != device; device = device->AttachedDevice)
    {
        if (CyberHiveIsMyDeviceObject(device))
        {
            CyberHiveDetachFromDevice(device);
            break;
        }
    }
}

NTSTATUS CyberHiveEnumerateFileSystemVolumes(
    __in PDEVICE_OBJECT DeviceObject
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    ULONG           numDevices = 0;
    ULONG           i = 0;
    PDEVICE_OBJECT  devList[DEVOBJ_LIST_SIZE];

    status = IoEnumerateDeviceObjectList(
        DeviceObject->DriverObject,
        devList,
        sizeof(devList),
        &numDevices);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    numDevices = min(numDevices, RTL_NUMBER_OF(devList));

    for (i = 0; i < numDevices; ++i)
    {
        //  Do not attach if:
        //      - This is the control device object (the one passed in)
        //      - The device type does not match
        //      - We are already attached to it.

        if (devList[i] != DeviceObject &&
            devList[i]->DeviceType == DeviceObject->DeviceType &&
            !CyberHiveIsAttachedToDevice(devList[i]))
        {
            status = CyberHiveAttachToDevice(devList[i], NULL);
        }

        ObDereferenceObject(devList[i]);
    }

    return STATUS_SUCCESS;
}

BOOLEAN CyberHiveIsAttachedToDevice(
    __in PDEVICE_OBJECT DeviceObject
)
{
    PDEVICE_OBJECT nextDevObj = NULL;
    PDEVICE_OBJECT currentDevObj = IoGetAttachedDeviceReference(DeviceObject);

    //
    //  Scan down the list to find our device object.
    //

    do
    {
        if (CyberHiveIsMyDeviceObject(currentDevObj))
        {
            ObDereferenceObject(currentDevObj);
            return TRUE;
        }

        nextDevObj = IoGetLowerDeviceObject(currentDevObj);

        ObDereferenceObject(currentDevObj);
        currentDevObj = nextDevObj;
    } while (NULL != currentDevObj);

    return FALSE;
}

NTSTATUS CyberHiveAttachToDevice(
    __in PDEVICE_OBJECT         DeviceObject,
    __out_opt PDEVICE_OBJECT* pFilterDeviceObject
)
{
    NTSTATUS                    status = STATUS_SUCCESS;
    PDEVICE_OBJECT              filterDeviceObject = NULL;
    PFSFILTER_DEVICE_EXTENSION  pDevExt = NULL;
    ULONG                       i = 0;

    ASSERT(!CyberHiveIsAttachedToDevice(DeviceObject));

    //
    //  Create a new device object we can attach with.
    //

    status = IoCreateDevice(
        g_driverObject,
        sizeof(FSFILTER_DEVICE_EXTENSION),
        NULL,
        DeviceObject->DeviceType,
        0,
        FALSE,
        &filterDeviceObject);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    pDevExt = (PFSFILTER_DEVICE_EXTENSION)filterDeviceObject->DeviceExtension;

    //
    //  Propagate flags from Device Object we are trying to attach to.
    //

    if (FlagOn(DeviceObject->Flags, DO_BUFFERED_IO))
    {
        SetFlag(filterDeviceObject->Flags, DO_BUFFERED_IO);
    }

    if (FlagOn(DeviceObject->Flags, DO_DIRECT_IO))
    {
        SetFlag(filterDeviceObject->Flags, DO_DIRECT_IO);
    }

    if (FlagOn(DeviceObject->Characteristics, FILE_DEVICE_SECURE_OPEN))
    {
        SetFlag(filterDeviceObject->Characteristics, FILE_DEVICE_SECURE_OPEN);
    }

    //
    //  Do the attachment.
    //
    //  It is possible for this attachment request to fail because this device
    //  object has not finished initializing.  This can occur if this filter
    //  loaded just as this volume was being mounted.
    //

    for (i = 0; i < 8; ++i)
    {
        LARGE_INTEGER interval;

        status = IoAttachDeviceToDeviceStackSafe(
            filterDeviceObject,
            DeviceObject,
            &pDevExt->AttachedToDeviceObject);

        if (NT_SUCCESS(status))
        {
            break;
        }

        //
        //  Delay, giving the device object a chance to finish its
        //  initialization so we can try again.
        //

        interval.QuadPart = (500 * DELAY_ONE_MILLISECOND);
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }

    if (!NT_SUCCESS(status))
    {
        //
        // Clean up.
        //

        IoDeleteDevice(filterDeviceObject);
        filterDeviceObject = NULL;
    }
    else
    {
        //
        // Mark we are done initializing.
        //

        ClearFlag(filterDeviceObject->Flags, DO_DEVICE_INITIALIZING);

        if (NULL != pFilterDeviceObject)
        {
            *pFilterDeviceObject = filterDeviceObject;
        }
    }

    return status;
}

void CyberHiveDetachFromDevice(
    __in PDEVICE_OBJECT DeviceObject
)
{
    PFSFILTER_DEVICE_EXTENSION pDevExt = (PFSFILTER_DEVICE_EXTENSION)
        DeviceObject->DeviceExtension;

    IoDetachDevice(pDevExt->AttachedToDeviceObject);
    IoDeleteDevice(DeviceObject);
}


VOID CyberHiveNotificationCallback(
    __in PDEVICE_OBJECT DeviceObject,
    __in BOOLEAN        FsActive
)
{
    if (FsActive)
        CyberHiveAttachToFileSystemDevice(DeviceObject);
    else
        CyberHiveDetachFromFileSystemDevice(DeviceObject);
}

VOID CyberHiveDriverUnload(
    __in PDRIVER_OBJECT DriverObject
)
{

    ULONG           numDevices = 0;
    ULONG           i = 0;
    LARGE_INTEGER   interval;
    PDEVICE_OBJECT  devList[DEVOBJ_LIST_SIZE];

    interval.QuadPart = (5 * DELAY_ONE_SECOND); //delay 5 seconds

    //
    //  Unregistered callback routine for file system changes.
    //
    IoUnregisterFsRegistrationChange(DriverObject, CyberHiveNotificationCallback);

    for (;;)
    {
        IoEnumerateDeviceObjectList(
            DriverObject,
            devList,
            sizeof(devList),
            &numDevices);

        if (0 == numDevices)
        {
            break;
        }

        numDevices = min(numDevices, RTL_NUMBER_OF(devList));

        for (i = 0; i < numDevices; ++i)
        {
            CyberHiveDetachFromDevice(devList[i]);
            ObDereferenceObject(devList[i]);
        }

        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }
}



NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //set global driver 
    
    g_driverObject = DriverObject;
    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = CyberHiveDriverEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config,
                           CyberHiveDriverEvtDeviceAdd
                           );

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             WDF_NO_HANDLE
                             );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    //set up passthrough for every call back IRP
    for(int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i)
        DriverObject->MajorFunction[i] = CyberHiveDispatchPassThrough;

    //Callback for file open/create
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CyberHiveDispatchCreate;


    DriverObject->DriverUnload = CyberHiveDriverUnload;

    return status;
}


NTSTATUS
CyberHiveDriverEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = CyberHiveDriverCreateDevice(DeviceInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

VOID
CyberHiveDriverEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    DriverObject - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Stop WPP Tracing
    //
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
}

