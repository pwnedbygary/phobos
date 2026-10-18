// psp-ge-profile's one static import from ARK: sctrlHENFindFunction, in SystemControl's SystemCtrlForKernel. Its
// NID is the first four bytes of the name's SHA-1, the scheme ARK (and PRO and M33 before it) exports under; every
// other ARK function the plugin uses is found through this one at run time (main.c). Written with pspsdk's
// pspimport.s macros rather than linking a stub library, so the plugin doesn't depend on ARK's or PRO's sources.
	.set noreorder

#include "pspimport.s"

	IMPORT_START "SystemCtrlForKernel",0x00090000
	IMPORT_FUNC  "SystemCtrlForKernel",0x159AF5CC,sctrlHENFindFunction
