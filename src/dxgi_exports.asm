EXTERN dxgi_exports:QWORD
.code
FORWARD MACRO name:req, index:req
name PROC
    jmp QWORD PTR [dxgi_exports + index * 8]
name ENDP
ENDM
FORWARD ApplyCompatResolutionQuirking, 0
FORWARD CompatString, 1
FORWARD CompatValue, 2
FORWARD DXGID3D10CreateDevice, 6
FORWARD DXGID3D10CreateLayeredDevice, 7
FORWARD DXGID3D10GetLayeredDeviceSize, 8
FORWARD DXGID3D10RegisterLayers, 9
FORWARD DXGIDeclareAdapterRemovalSupport, 10
FORWARD DXGIDisableVBlankVirtualization, 11
FORWARD DXGIDumpJournal, 12
FORWARD DXGIGetDebugInterface1, 13
FORWARD DXGIReportAdapterConfiguration, 14
FORWARD PIXBeginCapture, 15
FORWARD PIXEndCapture, 16
FORWARD PIXGetCaptureState, 17
FORWARD SetAppCompatStringPointer, 18
FORWARD UpdateHMDEmulationStatus, 19
END
