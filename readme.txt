Files in [SYSUTIL.RSCS] (NJE emulator):

ADDRESS.DAT       Temporary - holds the routine address for DMF in non-paged
                  pool. At the future, this routine will be loaded when the 
                  program is started.
BCB_CRC.C         Compute CRC and add envelope to outgoing blocks.
BRKDEF.H          Definitions for the $BRKTHRU system service.
COMPILE.COM       Compile all modules.
CONSTS.H          Defines common variables and constants.
CP.C              Control program (stand alone).
DMF.H             DMF definitions (needed for VMS_IO.C).
EBCDIC.H          EBCDIC characetrs and conversion tables.
EXOS.H            Include file to define EXOS types.
FILE_QUEUE.C      Queue files to the correct line.
HEADERS.C         various NJE commonly used blocks.
HEADERS.H         Definitions of the above.
IO.C              OS independent I/O routines.
LINK.COM          Link the modules of the detached process.
LOAD_DMF.MAR      Locad the DMF routine into non-paged pool.
LOAD_DMF_MAIN.C   Call the above routine.
MAIN.C            The main logic loop.
NJE.ROUTE         Indexed routing table.
NJE_BUILD.C       Builds the indexed routing table.
NJE_ROUTE.HDR     Local header for the routing table.
NMR.C             Handles the Nodal Messages Records.
PROTOCOL.C        The main protocol engine.
READ_CONFIG.C     Read the program's configuration from a file.
RECV_FILE.C       receive file from NJE line.
RUN.COM           Sample file to run the emulator. Our version is in SYSTARTUP
SENDCMDMSG.C      User's interface to send interactive messages and commands
                  to the network.
SEND_FILE.C       Send a file on NJE line.
TCP_COMMON.H      Framing routine for TCP (Same as the one in LOAD_DMF.MAR).
UNIX.DIR          The Unix couter-parts of the VMS*.C modules.
UTILS.C           Unitility routines.
VMS.C             VMS dependent routines.
VMS_DECNET        Do the DECnet processing.
VMS_TCP.C         EXOS/TCP and MultiNet I/O.
VMS_IO.C          VMS specific I/O routines (DMF mainly).
VMS_SEARCH_ROUTE.C Search a routing record in the indexed file.

The directory [.NEXT_VERSION] holds the in-development next version of this
emulator. The files in this directory are the latest working version.
